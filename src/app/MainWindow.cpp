#include "MainWindow.h"

#include "AboutDialog.h"
#include "ScriptFonts.h"
#include "FileProblem.h"
#include "DiagnosticLog.h"
#include "MultiSlot.h"
#include "MultiSlotDialog.h"
#include "SoftwareLists.h"
#include "ElWidgets.h"
#include "GameFontsDialog.h"
#include "GamesListDelegate.h"
#include "MachineFilterDialog.h"
#include "MameSettingsDialog.h"
#include "PreferencesDialog.h"
#include "TextFile.h"
#include "VclRuntime.h"
#include "ui_FormMain.h"
#include "ui_FormImageLayoutSelector.h"
#include "ImageLayouts.h"
#include "LayoutSettingsDialog.h"
#include "ui_FormImageCategorySelector.h"
#include "Favorites.h"
#include "ImageFinder.h"
#include "ImageView.h"
#include "ToolBarIcons.h"
#include "SettingsSearch.h"
#include "GameDetailsDialog.h"
#include "LastPlayedDialog.h"
#include "EmulatorsSetupDialog.h"
#include "DriverStatusDialog.h"
#include "ProgressWindow.h"
#include "SettingsDialog.h"
#include "SplashCustomizeDialog.h"
#include "RomsFoldersDialog.h"
#include "FileVersionsDialog.h"
#include "ExportGamesDialog.h"
#include "ScanSamplesDialog.h"
#include "ColumnsEditorDialog.h"
#include "GamesIconView.h"
#include "ThumbnailSettingsDialog.h"
#include "NightMode.h"
#include "NightModeDialog.h"
#include "SplashScreen.h"
#include "FavoritesManagerDialog.h"
#include "GameFilesDialog.h"
#include "CustomParametersDialog.h"
#include "RunGameExtraDialog.h"
#include "RunExtras.h"
#include "ImageDeleteRenameDialog.h"
#include "ImagesManagerDialog.h"
#include "RecycleBin.h"
#include "CategorySettingsDialog.h"
#include "VideoPreviewSettingsDialog.h"
#include "MiscFiltersDialog.h"
#include "ImagesDeleteClonesDialog.h"
#include "ui_FormImageLayoutDimensions.h"

#include <QPointer>
#include <QThreadPool>
#include <QElapsedTimer>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QGuiApplication>
#include <QFileDialog>
#include <QFontDialog>
#include <QCheckBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QShortcut>
#include <QFileInfo>
#include <QUrl>
#include <QDesktopServices>
#include <QScreen>
#include <QDialog>
#include <QStandardItemModel>
#include <QColorDialog>
#include <QScrollBar>
#include <QProgressDialog>
#include <QPainter>
#include <QRandomGenerator>
#include <QTimer>
#include <QActionGroup>
#include <QCursor>
#include <QTreeView>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace eld {

// The search bar's field (FilterGameTitle_ToolBar.Tag) and its settings menu items, in Tag order.
static const char* kSearchItems[GamesFilter::SearchFieldCount] = {
    "FilterGameTitle_Title", "FilterGameTitle_Year", "FilterGameTitle_Manufacturer", "FilterGameTitle_Category",
    "FilterGameTitle_VersionAdded", "FilterGameTitle_Language", "FilterGameTitle_DriverName", "FilterGameTitle_BiosName",
    "FilterGameTitle_ScreenResolution", "FilterGameTitle_GameName", "FilterGameTitle_CloneOf",
    "FilterGameTitle_SoftwareListName", "FilterGameTitle_NumberofPlayers"};


#ifndef EL_PRODUCT_NAME // set in CMakeLists.txt
#define EL_PRODUCT_NAME "EmuLoader ReLoaded"
#define EL_VERSION_LABEL ""
#endif

// Our own settings inside EmuLoader.ini ([EmuLoaderQt] in earlier builds: moved by Frontend::open).
static const char* kSection = "EmuLoaderRL";

MainWindow::MainWindow(Frontend& fe, QWidget* parent) : QWidget(parent), ui_(new Ui::FormMain), fe_(fe) {
    ui_->setupUi(this);
    vcl::apply(this);
    // Enable Diagnostic Log: main() opened it; a main window on another data folder opens its own
    if (DiagnosticLog::file() != fe_.userFile("diagnostic.log"))
        DiagnosticLog::open(fe_.userFile("diagnostic.log"), QStringLiteral(EL_PRODUCT_NAME " " EL_VERSION_LABEL " (build " EL_BUILD_REV ")"),
                            fe_.ini().readBool("Preferences", "DiagnosticLog", false), false);
    { // every window that opens goes in the log, by its title (the original logs its message boxes)
        struct DialogLog : QObject {
            using QObject::QObject;
            bool eventFilter(QObject* o, QEvent* e) override {
                if (e->type() == QEvent::Show && DiagnosticLog::enabled())
                    if (auto* d = qobject_cast<QDialog*>(o)) {
                        QString text = d->windowTitle();
                        if (auto* box = qobject_cast<QMessageBox*>(d)) text += "; " + box->text().left(300);
                        DiagnosticLog::write("dialog", QString("%1: %2").arg(QString::fromLatin1(d->metaObject()->className()), text));
                    }
                return false;
            }
        };
        static bool installed = false;
        if (!installed) {
            installed = true;
            qApp->installEventFilter(new DialogLog(qApp));
        }
    }
    // a name: the same in every language (while testing, with the version and build: which package it is)
#if defined(EL_TITLE_BUILD) && EL_TITLE_BUILD && defined(EL_BUILD_REV)
    setWindowTitle(QStringLiteral(EL_PRODUCT_NAME " " EL_VERSION_LABEL " (build " EL_BUILD_REV ")"));
#else
    setWindowTitle(QStringLiteral(EL_PRODUCT_NAME));
#endif
    setWindowIcon(QIcon(QDir(fe_.dataDir()).filePath("EmuLoader.ico")));
    if (fe_.emulatorFile(SystemId::MAME).isEmpty() && !fe_.emulatorFile(SystemId::HBMAME).isEmpty())
        sys_ = SystemId::HBMAME;

    // The images panel is positioned in code in the original; here it simply fills the
    // area under the images tool bar (and the picture fills the panel).
    ui_->PanelImagesDocuments->setProperty("vclAlign", "alClient");
    ui_->Images->setProperty("vclAlign", "alClient");
    vcl::relayout(ui_->PanelScreenshotsArea);
    vcl::relayout(ui_->PanelImage);

    model_ = new GamesModel(db_, fe_.resourcesFolder() + "gameslist_icons", this);
    night_ = fe_.ini().readBool("Preferences", "NightModeEnabled", false);
    {
        GameFonts f;
        f.setNight(night_);
        f.load(gameFontsFile());
        model_->setFonts(f);
    }
    filter_ = new GamesFilter(model_, this);
    ProgressWindow::setLook(StartupSplash::imagesFolder(fe_), StartupSplash::settingsFile(fe_));
    setupList();
    setupMenus();
    setupMachinesPanel();
    setGameIconSize(gameIconSize_, false); // tick the menu item, now that the menus exist
    setupToolBar();
    setupImagePanel();
    setupGameDocs();
    docsReady_ = true;
    restoreState();
    applyToolBarPlacement(); // Show Tool Bar, Bound to Games Panel
    applyPreferences();
    if (night_) applyNight(true, false); // [Preferences] NightModeEnabled
    loadGamesList();
    // MAME in full screen can change the screen's resolution; when it's put back, draw afresh.
    for (QScreen* sc : QGuiApplication::screens())
        connect(sc, &QScreen::geometryChanged, this, [this] { QTimer::singleShot(200, this, [this] { fullRepaint(); }); });
    if (fe_.imported()) QTimer::singleShot(0, this, [this] { firstStartImported(); });
    else QTimer::singleShot(0, this, [this] { startupSetupCheck(); }); // no emulator, or no games list
    StartupSplash::status({}, tr("Ready."));
}

// The first start in an EmuLoader folder: the user's data was copied from the original EmuLoader
// (Frontend::importOriginal). Says so, then makes the games list from MAME, as the original's
// isn't copied.
void MainWindow::firstStartImported() {
    if (StartupSplash::active() || !isVisible()) { // after the splash screen, over the main window
        QTimer::singleShot(100, this, [this] { firstStartImported(); });
        return;
    }
    setupPending_ = false;
    SetupAsking asking(this);
    QString text = fe_.importedFromCopy()
                       ? tr("<p>%1 keeps its settings in:<br><b>%2</b></p>"
                            "<p>Your settings, games lists, favorites, play history, custom parameters and image and video "
                            "settings were copied from your earlier copy of %1. Its files are not changed.</p>")
                             .arg(QStringLiteral(EL_PRODUCT_NAME), QDir::toNativeSeparators(fe_.userDir()))
                       : tr("<p>%1 now keeps its own settings, in:<br><b>%2</b></p>"
                            "<p>Your settings, favorites, play history, custom parameters and image and video settings were "
                            "copied from EmuLoader. EmuLoader's own files are not changed, now or later.</p>")
                             .arg(QStringLiteral(EL_PRODUCT_NAME), QDir::toNativeSeparators(fe_.userDir()));
    if (!fe_.importFailures().isEmpty()) {
        QStringList shown = fe_.importFailures().mid(0, 10);
        if (fe_.importFailures().size() > 10) shown << tr("... and %1 more").arg(fe_.importFailures().size() - 10);
        text += tr("<p>These files could not be copied, and were skipped:<br>%1</p>").arg(shown.join("<br>"));
    }
    bool canCreate = QFileInfo::exists(fe_.emulatorFile(sys_)) && !QFileInfo::exists(fe_.databaseFile(sys_));
    if (canCreate)
        text += tr("<p>The games list is made again from %1, so that it matches your version. Click OK to create it now.</p>")
                    .arg(Frontend::section(sys_));
    QMessageBox box(QMessageBox::Information, QStringLiteral(EL_PRODUCT_NAME), text, QMessageBox::Ok, this);
    box.setTextFormat(Qt::RichText);
    box.exec();
    if (canCreate) createGamesList();
    else startupSetupCheck(); // MAME isn't set up (or its file isn't there)
}

MainWindow::~MainWindow() {
    QThreadPool::globalInstance()->waitForDone(); // images still being read in the background
}

bool MainWindow::startMaximized() const {
    return fe_.ini().readString("Configuration", "WindowState", "Maximized") == "Maximized"; // (maximized the first time)
}

QIcon MainWindow::toolIcon(const QString& file, bool filters) const {
    return QIcon(filters ? fe_.resourcesFolder() + "main_icons/toolbar_filters/" + file : fe_.mainIcon(file));
}

// Port of TFormMain.AddDefaultIcons for the main tool bar: the picture of the icon, with the
// overlay (main_icons\overlay\toolbar_overlay[_multi]_light.ico) drawn on top. The caption that
// the original draws into the picture is cut off; ElButton shows it as text (ToolBarIcons).
QIcon MainWindow::toolbarIcon(const QString& file, bool filters, int overlay) const {
    QString path = filters ? fe_.resourcesFolder() + "main_icons/toolbar_filters/" + file : fe_.mainIcon(file);
    QString ov = overlay < 0 ? QString()
                             : fe_.resourcesFolder() + "main_icons/overlay/" +
                                   (overlay == 0 ? "toolbar_overlay_multi_light.ico" : "toolbar_overlay_light.ico");
    // The orb's picture has no caption strip: a square picture as tall as the other pictures.
    // The games filters' pictures have no caption either, but "List All" (allgames.ico).
    bool strip = file != "EmuLoader_Orb.ico" && file != "information.ico" && file != "msgbox_icons/error.ico" &&
                 (!file.startsWith("arcade_filters/") || file == "arcade_filters/allgames.ico");
    QSize pic = strip ? ToolBarIcons::pictureSize(toolIconSize_) : toolBarPictureSquare();
    return ToolBarIcons::make(path, ov, pic, strip);
}

// A square as tall as the tool bar pictures.
QSize MainWindow::toolBarPictureSquare() const {
    int h = ToolBarIcons::pictureSize(toolIconSize_).height();
    return {h, h};
}

void MainWindow::setToolBarIcon(ElButton* b, const QString& file, bool filters, int overlay) {
    b->setProperty("elIconFile", file);
    b->setProperty("elIconFilters", filters);
    b->setProperty("elIconOverlay", overlay);
    b->setIcon(toolbarIcon(file, filters, overlay));
    b->setCaption(ToolBarIcons::caption(file));
}

// TToolBar: visible buttons side by side from the left (Indent = 2), each with its caption under
// the picture. A button is as wide as its caption, up to one and a half pictures; a longer caption
// takes two lines. layoutToolBar sizes the buttons and the tool bar, placeToolBarButtons puts them
// in a row and moves the ones that don't fit into the » menu.
static bool onToolBar(ElButton* b) {
    return b->objectName() != "ToolBarOverflow" && (!b->isHidden() || b->property("elOverflow").toBool());
}

void MainWindow::layoutToolBar() {
    QSize pic = ToolBarIcons::pictureSize(toolIconSize_);
    QFont f = ui_->ToolBarButtons->font();
    f.setPixelSize(ToolBarIcons::captionPixelSize(toolIconSize_));
    QFontMetrics fm(f);
    int limit = std::max(64, pic.width() * 3 / 2);
    int lines = 1;
    QList<QPair<ElButton*, int>> widths;
    for (ElButton* btn : ui_->ToolBarButtons->findChildren<ElButton*>()) {
        if (!onToolBar(btn)) continue;
        btn->setFont(f);
        QString cap = btn->caption();
        int w = pic.width(), n = cap.isEmpty() ? 0 : 1;
        if (!cap.isEmpty()) {
            int one = fm.horizontalAdvance(cap);
            if (one <= limit) {
                w = std::max(w, one);
            } else {
                QRect r = fm.boundingRect(QRect(0, 0, limit, 1000), Qt::TextWordWrap | Qt::AlignHCenter, cap);
                w = std::max(w, r.width());
                n = std::min(2, std::max(1, r.height() / fm.height()));
            }
        }
        btn->setProperty("elCaptionLines", n);
        btn->setIconSize(cap.isEmpty() ? toolBarPictureSquare() : pic);
        lines = std::max(lines, n);
        widths.append({btn, w + 12});
    }
    toolButtonHeight_ = 3 + pic.height() + 2 + fm.height() * lines + 5;
    for (auto [btn, w] : widths) btn->resize(w, toolButtonHeight_);
    // The tool bar is as tall as its buttons, and the CPU filter's row under them when it's shown.
    int h = std::max(toolButtonHeight_ + 4, 52);
    if (ui_->PanelFilterCPU->isVisibleTo(ui_->PanelToolBar)) h += ui_->PanelFilterCPU->height(); // the CPU filter's row
    vcl::place(ui_->PanelToolBar, QRect(0, 0, ui_->PanelToolBar->width(), h));
    vcl::relayout(this);
    if (ui_->PanelToolBar->parentWidget() != this) vcl::relayout(ui_->PanelToolBar->parentWidget());
    vcl::relayout(ui_->PanelToolBar);
    placeToolBarButtons();
}

void MainWindow::placeToolBarButtons() {
    QWidget* bar = ui_->ToolBarButtons;
    if (!overflow_) {
        overflow_ = new ElButton(bar);
        overflow_->setObjectName("ToolBarOverflow");
        overflow_->setText(QString::fromUtf8("\u00BB"));
        overflow_->setToolTip(tr("More tool bar buttons"));
        overflow_->setFlat(true);
        overflow_->setFocusPolicy(Qt::NoFocus);
        overflow_->setStyleSheet("QPushButton { border: none; background: transparent; font-size: 16px; }"
                                 "QPushButton:hover { background: rgba(0, 120, 215, 30); }");
        connect(overflow_, &QPushButton::clicked, this, &MainWindow::showToolBarOverflow);
    }
    // in the tool bar's order: buttons, separators (a line) and spaces (the room left, shared)
    struct Item { ElButton* button = nullptr; QWidget* separator = nullptr; bool space = false; };
    QList<Item> items;
    int sep = 0;
    for (const QString& t : toolOrder_) {
        if (t == "space") items.append({nullptr, nullptr, true});
        else if (t == "|") {
            if (QWidget* f = toolSeparators_.value(sep++)) items.append({nullptr, f, false});
        }
        else if (auto* b = bar->findChild<ElButton*>(t); b && onToolBar(b)) items.append({b, nullptr, false});
    }
    constexpr int sepWidth = 10;
    const int avail = bar->width(), more = 24;
    int total = 2, spaces = 0;
    for (const Item& it : items) {
        if (it.button) total += it.button->width();
        else if (it.separator) total += sepWidth;
        else ++spaces;
    }
    bool overflowing = total > avail;
    const int share = overflowing || spaces == 0 ? 0 : (avail - total) / spaces;
    int x = 2;
    bool cut = false;
    for (const Item& it : items) {
        if (it.space) {
            x += share;
            continue;
        }
        const int w = it.button ? it.button->width() : sepWidth;
        bool fits = !overflowing || (!cut && x + w + more <= avail);
        cut = cut || !fits; // the rest go too, in order
        if (it.separator) {
            it.separator->setVisible(fits);
            if (fits) {
                it.separator->setGeometry(x + sepWidth / 2 - 1, 6, 2, std::max(8, toolButtonHeight_ - 12));
                x += sepWidth;
            }
            continue;
        }
        ElButton* b = it.button;
        if (fits && b->property("elOverflow").toBool()) {
            b->setProperty("elOverflow", false);
            b->show();
        } else if (!fits && !b->property("elOverflow").toBool()) {
            b->setProperty("elOverflow", true);
            b->hide();
        }
        if (!fits) continue;
        vcl::place(b, QRect(x, 0, b->width(), toolButtonHeight_));
        x += b->width();
    }
    overflow_->setVisible(overflowing);
    if (overflowing) {
        vcl::place(overflow_, QRect(x, 0, more, toolButtonHeight_));
        overflow_->raise();
    }
}

// The tool bar's buttons in their order ([ToolBar] GamesFilters_Buttons), not the » one
QList<ElButton*> MainWindow::toolBarButtons() const {
    QList<ElButton*> out;
    for (ElButton* b : ui_->ToolBarButtons->findChildren<ElButton*>())
        if (b->objectName() != "ToolBarOverflow" && b->objectName() != "ButtonGuide" && b->objectName() != "ButtonMameProblems")
            out << b;
    return out;
}

// The » button: the buttons that don't fit, as a menu.
void MainWindow::showToolBarOverflow() {
    QMenu menu(this);
    for (ElButton* b : ui_->ToolBarButtons->findChildren<ElButton*>()) {
        if (!b->property("elOverflow").toBool()) continue;
        QString text = b->caption().isEmpty() ? b->toolTip().section('\n', 0, 0) : b->caption();
        QAction* a = menu.addAction(b->icon(), text);
        a->setEnabled(b->isEnabled());
        a->setToolTip(b->toolTip());
        auto* sub = qobject_cast<QMenu*>(b->property("elMenu").value<QObject*>());
        connect(a, &QAction::triggered, this, [b, sub] {
            if (sub) QTimer::singleShot(0, sub, [sub] { sub->exec(QCursor::pos()); });
            else b->click();
        });
    }
    menu.exec(overflow_->mapToGlobal(QPoint(0, overflow_->height())));
}

// Menu Options > Games List > Tool Bar Buttons > Icon Size: [ToolBar] GamesFilters_IconSize.
void MainWindow::setToolBarIconSize(int idx) {
    fe_.ini().writeInteger("ToolBar", "GamesFilters_IconSize", idx);
    toolIconSize_ = idx == 0 ? QSize(68, 68) : idx == 2 ? QSize(30, 24) : QSize(48, 48);
    for (ElButton* b : ui_->ToolBarButtons->findChildren<ElButton*>())
        if (b->property("elIconFile").isValid())
            setToolBarIcon(b, b->property("elIconFile").toString(), b->property("elIconFilters").toBool(),
                           b->property("elIconOverlay").toInt());
    problemsIcon(); // (gray once seen)
    layoutToolBar();
    layoutSearchPanel();
}

// FilterSearchBarAdjustPanelControls: the search panel for the tool bar's icon size. With the
// small tool bar (30x24) the text box moves up beside its label, which shows only with "Show
// Filter Labels in Small Tool Bar"; "Use Small Icons" makes its buttons 16x16.
void MainWindow::layoutSearchPanel() {
    const int sizeIdx = fe_.ini().readInteger("ToolBar", "GamesFilters_IconSize", 1);
    const bool smallIcons = fe_.ini().readBool("ListFilter", "SearchBarUseSmallIcons", false);
    const bool labels = fe_.ini().readBool("ListFilter", "SearchBarBarShowFilterLabels", true);
    QLabel* label = ui_->LabelSearchGamesBy_ToolBar;
    QLineEdit* ed = ui_->FilterGameTitle_ToolBar;
    label->setVisible(sizeIdx != 2 || labels);
    const int labelTop = sizeIdx == 0 ? 16 : sizeIdx == 2 ? 8 : 6;
    vcl::place(label, QRect(8, labelTop, label->sizeHint().width(), label->height()));
    int edLeft = 8;
    if (sizeIdx == 2 && labels) edLeft = label->x() + label->width() + 3;
    vcl::place(ed, QRect(edLeft, sizeIdx == 2 ? 3 : labelTop + 16, 189, 24));
    // the buttons after the text box (MoveButton)
    const int w = smallIcons ? 22 : 26, top = ed->y() + (smallIcons ? 1 : -1);
    int x = ed->x() + ed->width() + 3;
    for (QPushButton* b : {(QPushButton*)ui_->ButtonFilterTitleApply_ToolBar, (QPushButton*)ui_->ButtonFilterControls_ToolBar,
                           (QPushButton*)ui_->ButtonFilterTitleReset_ToolBar, (QPushButton*)ui_->ButtonFilterTitleSettings_ToolBar}) {
        b->setIconSize(smallIcons ? QSize(16, 16) : QSize(24, 24));
        vcl::place(b, QRect(x, top, w, w + 1));
        x += w + 1;
    }
    QWidget* panel = ui_->PanelSearchGames_ToolBar;
    vcl::place(panel, QRect(panel->x(), panel->y(), x + 7, panel->height()));
    if (panel->parentWidget()) vcl::relayout(panel->parentWidget());
    placeToolBarButtons();
}

// ---------------------------------------------------------------- toolbar

void MainWindow::setupToolBar() {
    // IL_ToolBarButtons in ImageIndex order (TFormMain.LoadToolBarButtons): file, folder, overlay index.
    struct Ico { const char* file; bool filters; int overlay; };
    static const Ico icons[] = {
        {"EmuLoader_Orb.ico", false, -1}, {"viewmode_01_details.ico", true, 1}, {"filter5_machines.ico", true, 1},
        {"filter_systems_quick_disabled.ico", true, 1}, {"favorites.ico", true, 1}, {"arcade_filters/allgames.ico", false, 0},
        {"filter2_01_both.ico", true, 0}, {"filter3_01_listall.ico", true, 0}, {"filter4_01_all.ico", true, 0},
        {"mamu_.ico", false, 1}, {"filter_extra.ico", true, 1}, {"cdimage_cue_enabled.ico", true, 1},
        {"emu_useparameter.ico", true, 1}, {"games_mru.ico", true, 1}, {"emulators.ico", true, 1},
        {"filter_search_text.ico", true, 1}};
    // [ToolBar] GamesFilters_IconSize: 0 -> 68x68, 1 -> 48x48 (default, as in the original), 2 -> 30x24
    int sizeIdx = fe_.ini().readInteger("ToolBar", "GamesFilters_IconSize", 1);
    toolIconSize_ = sizeIdx == 0 ? QSize(68, 68) : sizeIdx == 2 ? QSize(30, 24) : QSize(48, 48);
    // [ToolBar] GamesFilters_Buttons: one 0/1 per button in tool bar order, then the search panel.
    QString vis = fe_.ini().readString("ToolBar", "GamesFilters_Buttons");
    const QList<ElButton*> buttons = ui_->ToolBarButtons->findChildren<ElButton*>();
    for (int i = 0; i < buttons.size(); ++i) {
        ElButton* b = buttons[i];
        int idx = b->property("vclImageIndex").isValid() ? b->property("vclImageIndex").toInt() : -1;
        b->setToolTip(b->toolTip().isEmpty() ? b->text() : b->toolTip());
        b->setProperty("elDesignText", b->text()); // "Games Filters-MAME and Arcade" (Customize Tool Bar)
        b->setText({});
        b->setFlat(true);
        // As TToolBar's buttons, they don't take the focus: a button that loses it isn't drawn
        // pressed any more (QAbstractButton::focusOutEvent), which the Misc and Favorites
        // buttons use to show they're on.
        b->setFocusPolicy(Qt::NoFocus);
        b->setStyleSheet("QPushButton { border: none; background: transparent; }"
                         "QPushButton:hover { background: rgba(0, 120, 215, 30); }"
                         "QPushButton:pressed, QPushButton:checked { background: rgba(0, 120, 215, 60); }");
        if (idx >= 0 && idx < int(std::size(icons))) {
            setToolBarIcon(b, icons[idx].file, icons[idx].filters, icons[idx].overlay);
            b->setProperty("elOverlay", icons[idx].overlay);
        }
        if (i < vis.size()) b->setVisible(vis[i] == '1');
        b->setProperty("elShown", i >= vis.size() || vis[i] == '1'); // [ToolBar] GamesFilters_Buttons
        if (b == ui_->ButtonGameFilterFavorites) {
            // ButtonGameFilterFavoritesMouseUp: left click toggles the favorites list,
            // right click opens the favorites menu.
            connect(b, &QPushButton::clicked, this, [this] {
                if (QAction* a = menus_.action("PopupEnableFavorites")) a->trigger();
            });
            b->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(b, &QWidget::customContextMenuRequested, this, [this, b] {
                if (QMenu* m = menus_.menu("PopupFavorites")) m->exec(b->mapToGlobal(QPoint(8, b->height())));
            });
            auto* toggle = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F3), this); // Ctrl+F3 toggle ON/OFF
            connect(toggle, &QShortcut::activated, b, &QPushButton::click);
            continue;
        }
        if (b == ui_->ButtonGameFilterMachineTypeSystemsMulti) {
            connect(b, &QPushButton::clicked, this, &MainWindow::openMachineFilter);
            continue;
        }
        if (b == ui_->ButtonExtraFilters) {
            connect(b, &QPushButton::clicked, this, &MainWindow::openMiscFilters);
            continue;
        }
        if (b == ui_->ButtonArcadeGamesFilters) {
            b->setProperty("elBaseTip", b->toolTip().section('\n', 0, 0));
            connect(b, &QPushButton::clicked, this, &MainWindow::openGamesFilters);
            continue;
        }
        if (b == ui_->ButtonMAMu_Icons) {
            // left click turns MAMu_ icons on or off, right click opens their menu (PopupMAMu_Icons)
            connect(b, &QPushButton::clicked, this, [this] {
                if (QAction* a = menus_.action("PopupEnableMAMu_Icons")) a->trigger();
            });
            b->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(b, &QWidget::customContextMenuRequested, this, [this, b] {
                if (QMenu* m = menus_.menu("PopupMAMu_Icons")) m->exec(b->mapToGlobal(QPoint(8, b->height())));
            });
            continue;
        }
        if (b == ui_->ButtonGamesMRU) {
            connect(b, &QPushButton::clicked, this, &MainWindow::showLastPlayed);
            continue;
        }
        if (b == ui_->ButtonCustomEmuParameterToUse) {
            // The original's "Emulator Parameters To Use" was for other emulators: here it opens the
            // selected game's custom parameters (as the games list menu's Set Game Parameters).
            b->setProperty("elDesignText", tr("Custom Parameters-Selected Game"));
            b->setToolTip(tr("Custom parameters for the selected game"));
            connect(b, &QPushButton::clicked, this, &MainWindow::customParametersButton);
            continue;
        }
        QString menu = b->property("vclDropdownMenu").toString();
        if (QMenu* m = menus_.menu(menu)) {
            b->setProperty("elMenu", QVariant::fromValue<QObject*>(m)); // for the » menu
            connect(b, &QPushButton::clicked, this, [b, m] { m->exec(b->mapToGlobal(QPoint(0, b->height()))); });
        } else {
            // Not ported yet: keep the original look (no grayed-out icon), but do nothing.
            QIcon ic = b->icon();
            for (const QSize& sz : ic.availableSizes()) ic.addPixmap(ic.pixmap(sz), QIcon::Disabled);
            b->setIcon(ic);
            b->setEnabled(false);
            b->setToolTip(tr("%1 (not ported to the C++ version yet)").arg(b->toolTip()));
        }
    }
    if (!vis.isEmpty() && vis.endsWith('0')) ui_->PanelSearchGames_ToolBar->hide();
    setupGuideButton();
    setupProblemsButton();
    setupCpuFilter();
    loadToolOrder(); // [EmuLoaderRL] ToolBarOrder: the buttons' order, separators and spaces
    ui_->ToolBarButtons->installEventFilter(this); // a narrower window moves buttons into the » menu
    // Menu Options > Games List > Tool Bar Buttons > Icon Size
    auto* sizes = new QActionGroup(this);
    const char* sizeMenus[] = {"MenuToolBarIconSizeExtraLarge", "MenuToolBarIconSizeLarge", "MenuToolBarIconSizeSmall"};
    for (int i = 0; i < 3; ++i)
        if (QAction* a = menus_.action(sizeMenus[i])) {
            a->setCheckable(true);
            a->setChecked(i == sizeIdx);
            sizes->addAction(a);
            menus_.on(sizeMenus[i], [this, i] { setToolBarIconSize(i); });
        }
    layoutToolBar();
    // Driver status filter (ButtonGameFilterDriverStatusClick): FormArcadeFiltersDriverStatus under
    // the button.
    ui_->ButtonGameFilterDriverStatus->setEnabled(true);
    connect(ui_->ButtonGameFilterDriverStatus, &QPushButton::clicked, this, [this] {
        const char* drvIcons[] = {"filter4_01_all.ico", "filter4_02_good-imperfect.ico", "filter4_03_good.ico",
                                  "filter4_04_imperfect.ico", "filter4_05_preliminary.ico"};
        QList<QIcon> icons;
        for (const char* f : drvIcons) icons << toolIcon(f);
        DriverStatusDialog dlg(filter_->driver(), filter_->driverCategory(), icons, this);
        dlg.move(popupPos(ui_->ButtonGameFilterDriverStatus, dlg.size()));
        if (dlg.exec() != QDialog::Accepted) return;
        if (dlg.status() == filter_->driver() && dlg.category() == filter_->driverCategory()) return;
        rememberSelection();
        filter_->clearSearch();
        filter_->setDriverCategory(dlg.category());
        filter_->setDriver(dlg.status());
        updateFilterIcons();
        afterFilterChange();
    });

    // Search panel (PanelSearchGames_ToolBar): Enter or Apply searches, Esc clears the text,
    // Reset goes back to the tool bar filters, Settings picks the field to search.
    QLineEdit* ed = ui_->FilterGameTitle_ToolBar;
    ed->setClearButtonEnabled(false);
    ed->installEventFilter(this);
    connect(ed, &QLineEdit::returnPressed, this, &MainWindow::applySearch);
    connect(ui_->ButtonFilterTitleApply_ToolBar, &QPushButton::clicked, this, &MainWindow::applySearch);
    connect(ui_->ButtonFilterTitleReset_ToolBar, &QPushButton::clicked, this, [this] {
        ui_->FilterGameTitle_ToolBar->clear();
        rememberSelection();
        filter_->clearSearch();
        afterFilterChange();
    });
    connect(ui_->ButtonFilterTitleSettings_ToolBar, &QPushButton::clicked, this, [this] {
        if (QMenu* m = menus_.menu("PopupFilterGameTitle"))
            m->exec(ui_->ButtonFilterTitleSettings_ToolBar->mapToGlobal(QPoint(0, ui_->ButtonFilterTitleSettings_ToolBar->height())));
    });
    for (auto [b, file] : {std::pair{ui_->ButtonFilterTitleApply_ToolBar, "search.ico"},
                           std::pair{ui_->ButtonFilterControls_ToolBar, "interface_classic.ico"},
                           std::pair{ui_->ButtonFilterTitleReset_ToolBar, "refresh.ico"},
                           std::pair{ui_->ButtonFilterTitleSettings_ToolBar, "settings.ico"}}) {
        QIcon ic = toolIcon(file, false);
        for (const QSize& sz : ic.availableSizes()) ic.addPixmap(ic.pixmap(sz), QIcon::Disabled);
        b->setIcon(ic);
        b->setIconSize(QSize(24, 24));
        b->setFlat(true);
        b->setText({});
    }
    ui_->ButtonFilterTitleReset_ToolBar->setToolTip(tr("Click here to clear the search and show the tool bar filters again"));
    // Use Small Icons, Show Filter Labels in Small Tool Bar ([ListFilter]), Help
    for (auto [item, key, def] : {std::tuple{"FilterSearchBarUseSmallIcons", "SearchBarUseSmallIcons", false},
                                  std::tuple{"FilterSearchBarShowFilterLabels", "SearchBarBarShowFilterLabels", true}}) {
        if (QAction* a = menus_.action(item)) {
            a->setCheckable(true);
            a->setChecked(fe_.ini().readBool("ListFilter", key, def));
        }
        menus_.on(item, [this, item = QString(item), key = QString(key)] {
            QAction* a = menus_.action(item);
            fe_.ini().writeInteger("ListFilter", key, a && a->isChecked() ? 1 : 0);
            layoutSearchPanel();
        });
    }
    menus_.on("FilterSearchBarHelp", [this] { searchHelp(); });
    layoutSearchPanel();
    // Controls (ButtonFilterControls_ToolBarClick): the games with a control, from a menu of
    // mame_control_type.ini's control types (PopupSearchBarControlsFilter)
    connect(ui_->ButtonFilterControls_ToolBar, &QPushButton::clicked, this, &MainWindow::showControlsFilterMenu);
    ui_->ButtonFilterControls_ToolBar->setToolTip(tr("Show the games with a control: joystick, trackball, paddle ..."));

    // Images tool bar (IL_ImagesToolBarButtons)
    const QHash<QString, QString> imgIcons = {
        {"ButtonPreviousImage", "image_prev.ico"}, {"ButtonNextImage", "image_next.ico"},
        {"ButtonPreviousCategory", "image_prev_category.ico"}, {"ButtonImageCategory", "image_01_gamesnap.ico"},
        {"ButtonNextCategory", "image_next_category.ico"}, {"ButtonPreviousLayout", "image_prev_layout.ico"},
        {"ButtonScreenshotLayouts", "img1_layout1.ico"}, {"ButtonNextLayout", "image_next_layout.ico"},
        {"ButtonVideoPreviewPlay", "video_preview.ico"}, {"ButtonInternetGameInfo", "internet_gameinfo.ico"},
        {"ButtonOpenManualPDF", "game_manual.ico"}, {"ButtonImagePanelToggle", "image_view.ico"},
        {"ButtonDocsPanelToggle", "gamedocs.ico"}};
    for (ElButton* b : ui_->ImagesToolBarButtons->findChildren<ElButton*>()) {
        b->setToolTip(b->toolTip().isEmpty() ? b->text() : b->toolTip());
        b->setText({});
        b->setFlat(true);
        b->setIconSize(QSize(24, 24));
        if (imgIcons.contains(b->objectName())) b->setIcon(toolIcon(imgIcons.value(b->objectName()), false));
        b->setEnabled(false); // enabled as they're hooked up (setupImagePanel)
        QIcon ic = b->icon();
        for (const QSize& sz : ic.availableSizes()) ic.addPixmap(ic.pixmap(sz), QIcon::Disabled);
        b->setIcon(ic);
        b->setStyleSheet("QPushButton { border: none; background: transparent; }"
                         "QPushButton:checked { background: rgba(0, 120, 215, 60); border-radius: 2px; }");
    }
    // Images tool bar buttons side by side, vertically centered in the 31 px bar.
    layoutImageToolBar();
    // Tool bar buttons never take the focus (TToolButton / TSpeedButton), so the games list
    // keeps it and its selection bar keeps its active colors.
    for (QWidget* bar : {ui_->ToolBarButtons, ui_->ImagesToolBarButtons})
        for (ElButton* b : bar->findChildren<ElButton*>()) b->setFocusPolicy(Qt::NoFocus);
    updateFilterIcons();
}

void MainWindow::updateFilterIcons() {
    static const char* hm[] = {"filter3_01_listall.ico", "filter3_02_have.ico", "filter3_03_miss.ico",
                               "filter3_04_missroms.ico", "filter3_05_have_and_missroms.ico", "filter3_06_miss_and_missroms.ico"};
    static const char* pc[] = {"filter2_01_both.ico", "filter2_02_parent.ico", "filter2_03_clone.ico"};
    static const char* dr[] = {"filter4_01_all.ico", "filter4_02_good-imperfect.ico", "filter4_03_good.ico",
                               "filter4_04_imperfect.ico", "filter4_05_preliminary.ico"};
    setToolBarIcon(ui_->ButtonGameFilterHaveMiss, hm[filter_->haveMiss()], true, 0);
    setToolBarIcon(ui_->ButtonGameFilterParentClone, pc[filter_->parentClone()], true, 0);
    setToolBarIcon(ui_->ButtonGameFilterDriverStatus, dr[filter_->driver()], true, 0);
    { // SetMainToolBarIcon(ButtonArcadeGamesFilters): the filter's picture (no_icon.ico if it has none)
        QString name = filter_->arcadeFilter();
        QString file = "arcade_filters/" + name + ".ico";
        if (!QFileInfo::exists(fe_.resourcesFolder() + "main_icons/" + file)) file = "arcade_filters/no_icon.ico";
        setToolBarIcon(ui_->ButtonArcadeGamesFilters, file, false, 0);
        if (!QFileInfo::exists(fe_.resourcesFolder() + "main_icons/arcade_filters/" + name + ".ico"))
            ui_->ButtonArcadeGamesFilters->setCaption(name);
        ui_->ButtonArcadeGamesFilters->setToolTip(ui_->ButtonArcadeGamesFilters->property("elBaseTip").toString() +
                                                  "\n -> " + arcadeFilterTitle(name));
    }
    // pressed while in use (New for Misc). The buttons are checkable (the original's speed buttons:
    // a click checks them), so their checked state follows too, or they'd look pressed when not.
    auto pressed = [](QAbstractButton* b, bool on) {
        b->setDown(on);
        if (b->isCheckable()) b->setChecked(on);
    };
    pressed(ui_->ButtonExtraFilters, !filter_->miscFilters().isDefault());
    if (toolButtonHeight_) layoutToolBar(); // a caption may have changed its width
    pressed(ui_->ButtonGameFilterFavorites, filter_->favoritesOnly());
}

// ------------------------------------------------------------------ menus

void MainWindow::setupMenus() {
    menus_.load(":/forms/FormMain.menus.json", this);
    setupIconView(); // the view modes and the thumbnails' menus
    // Have/Miss and Parent/Clone filters: item order = filter value.
    const char* hm[] = {"PopupShowHaveMiss", "PopupShowHave", "PopupShowMiss", "PopupShowMissROMs",
                        "PopupShowHaveAndMissROMs", "PopupShowMissAndMissROMs"};
    for (int i = 0; i < 6; ++i)
        menus_.on(hm[i], [this, i] { rememberSelection(); filter_->clearSearch(); filter_->setHaveMiss(i); updateFilterIcons(); afterFilterChange(); });
    const char* pc[] = {"PopupShowParentClone", "PopupShowParent", "PopupShowClone"};
    for (int i = 0; i < 3; ++i)
        menus_.on(pc[i], [this, i] { rememberSelection(); filter_->clearSearch(); filter_->setParentClone(i); updateFilterIcons(); afterFilterChange(); });
    menus_.on("PopupEnableFavorites", [this] {
        rememberSelection();
        filter_->clearSearch();
        filter_->setFavoritesOnly(!filter_->favoritesOnly());
        if (QAction* a = menus_.action("PopupEnableFavorites")) a->setChecked(filter_->favoritesOnly());
        updateFilterIcons();
        afterFilterChange();
    });
    menus_.on("PopupFavoritesManager", [this] { openFavoritesManager(); });
    // Games list menu > Delete Selected Games Files, Copy/Move > ... (PopupDeleteSelectedGamesClick, tag 0/1/2)
    menus_.on("PopupDeleteSelectedGames", [this] { gameFiles(FileAction::Delete); });
    menus_.on("PopupPlayGameExtraParametersMAME", [this] { runGameExtra(); });
    menus_.on("PopupEditGameCustomParameters", [this] { customParameters(ParamsGame, false); });
    menus_.on("PopupDeleteGameCustomParameters", [this] { customParameters(ParamsGame, true); });
    menus_.on("PopupEditParentGameCustomParameters", [this] { customParameters(ParamsParent, false); });
    menus_.on("PopupDeleteParentGameCustomParameters", [this] { customParameters(ParamsParent, true); });
    menus_.on("PopupEditSoftwareListCustomParameters", [this] { customParameters(ParamsSoftwareList, false); });
    menus_.on("PopupDeleteSoftwareListCustomParameters", [this] { customParameters(ParamsSoftwareList, true); });
    menus_.on("PopupEditMAMEMachineCustomParameters", [this] { customParameters(ParamsMachine, false); });
    menus_.on("PopupDeleteMAMEMachineCustomParameters", [this] { customParameters(ParamsMachine, true); });
    // MAME software list games
    menus_.on("PopupMachineToRunGame", [this] {
        if (const MameGame* g = current(); g && g->isSoftware()) selectMachine(*g, true);
    });
    menus_.on("MenuCreateMAMESoftwareListGames", [this] { createSoftwareListGames(); });
    menus_.on("MenuCreateHBMAMESoftwareListGames", [this] { createSoftwareListGames(); });
    menus_.on("PopupCopySelectedGames", [this] { gameFiles(FileAction::Copy); });
    menus_.on("PopupMoveSelectedGames", [this] { gameFiles(FileAction::Move); });
    menus_.on("MenuExit", [this] { close(); });
    menus_.on("MenuPreferences", [this] { openSettings(); });
    if (QAction* a = menus_.action("MenuPreferences")) a->setMenuRole(QAction::PreferencesRole); // macOS: the app menu, Cmd+,
    for (int i = 0; i < GamesFilter::SearchFieldCount; ++i) menus_.on(kSearchItems[i], [this, i] { setSearchField(i); });
    menus_.on("FilterTextBarIncludeToolBarFilters", [] {}); // a check box, read when searching
    {
        const char* items[5] = {"PopupIconSizeMegaLarge", "PopupIconSizeExtraLarge", "PopupIconSizeLarge",
                                "PopupIconSizeStandard", "PopupIconSizeSmall"};
        for (int i = 0; i < 5; ++i) menus_.on(items[i], [this, i] { setGameIconSize(i, true); });
    }
    menus_.on("MenuFontSettings", [this] { openFontSettings(); });
    menus_.on("PopupFontSettings", [this] { openFontSettings(); });
    // Either can change the image folders (MAME's folder, snapshot_directory).
    // Games list menu > Game Details (PopupGameDetailsClick)
    menus_.on("PopupGameDetails", [this] {
        const MameGame* g = current();
        if (!g) return;
        GameDetailsDialog::Input in;
        in.game = *g;
        in.nameLine = ui_->StatusBar_GamesGameName->text();
        in.gameIcon = model_->icon(*g);
        in.players = model_->playersText(*g);
        in.emuIcon = fe_.mainIcon(sys_ == SystemId::HBMAME ? "emu_hbmame.ico" : "emu_mame.ico");
        // FillGameDetails: "Nx  01:02:03"
        in.game.driver = g->driverText(model_->options().driverPath);
        in.lastPlayed = PlayedGames::dateText(g->lastPlayed, model_->options().hideSecondsPlayed);
        if (g->played > 0 || g->playtime > 0)
            in.playtime = QString("%1x  ").arg(g->played) +
                          PlayedGames::playtimeText(g->playtime, model_->options().hideSecondsPlaytime);
        GameDetailsDialog dlg(fe_, sys_, in, this);
        dlg.exec();
    });
    // Games list menu > Reset Selected Games Info (PopupResetSelectedGamesInfo_AllClick: the tag)
    menus_.on("PopupResetSelectedGamesInfo_Playtime", [this] { resetPlayInfo(1); });
    menus_.on("PopupResetSelectedGamesInfo_LastPlayed", [this] { resetPlayInfo(2); });
    menus_.on("PopupResetSelectedGamesInfo_TimesPlayed", [this] { resetPlayInfo(3); });
    menus_.on("PopupResetSelectedGamesInfo_All", [this] { resetPlayInfo(0); });
    // Tool Bar Buttons > Customize, or a right-click on the tool bar (ToolBarButtonsMouseDown)
    menus_.on("MenuCustomizeToolBar", [this] { openSettings("Tool Bar"); });
    ui_->ToolBarButtons->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui_->ToolBarButtons, &QWidget::customContextMenuRequested, this, [this] { openSettings("Tool Bar"); });
    menus_.on("MenuArcadeEmulatorSetup", [this] { openSettings("Emulators"); });
    menus_.on("MenuArcadeEmulatorDefaultSettings", [this] { openEmulatorSettings(); refreshImageFolders(); });
    menus_.on("MenuCreateArcadeGamesList", [this] { createGamesList(); });
    menus_.on("MenuEmulatorReloadROMsFolders", [this] { rescanRoms(); });
    // Help: web links. The URL is the item's hint in the original; sites that have moved get their
    // current address here, and sites that are gone (checked November 2026) are left out.
    static const std::pair<const char*, const char*> links[] = {
        {"MenuVisitEmuLoaderHomepage", nullptr}, {"MenuVisitEmuLoaderForum", nullptr},
        {"MenuVisitMAMEHomepage", "https://www.mamedev.org/"},
        {"MenuVisitHBMAMEHomepage", nullptr},
        {"MenuVisitSupermodelHomepage", nullptr},
        {"MenuVisitDICEHomepage", "https://sourceforge.net/projects/dice/"},
        {"MenuVisitMrDoArcadeHomepage", "https://mameworld.info/mrdo/"},
        {"MenuVisitClrMAMEProHomepage", nullptr},
        {"MenuVisitXArcadeHomepage", nullptr},
        {"MenuVisitMashMameInfo", "https://mashinfo.github.io/mameinfo/"},
        {"MenuVisitEasyEmu", "https://easyemu.mameworld.info/"},
        {"MenuHelpMAMEDocsWiki", "https://wiki.mamedev.org/"},
        {"HelpMAMEDocumentation", "https://docs.mamedev.org/"},
    };
    for (const auto& [name, url] : links)
        if (QAction* a = menus_.action(name)) {
            QString u = url ? QString(url) : a->property("hint").toString();
            a->setToolTip(u);
            menus_.on(name, [u] { QDesktopServices::openUrl(QUrl(u)); });
        }
    // Left out: sites that are gone (ZiNc's emuhype.com, Daphne, Progetto EMMA and its Catlist, MAWS,
    // Hanaho's Hot Rod and SlikStik shops), Catlist (not updated since 2007), and the ones that
    // couldn't be confirmed (Treyonics' Devastator II, MAMu_ icons, Demul, Model 2).
    for (const char* n : {"MenuVisitZiNcHomepage", "MenuVisitDaphneHomepage", "MenuVisitDemulHomepage",
                          "MenuVisitSEGAModel2Homepage", "MenuVisitProgettoEMMAHomepage", "MenuVisitCatlistEMMA",
                          "MenuVisitMAWSHomepage", "MenuVisitHotRodHomepage", "MenuVisitSlikStikHomepage",
                          "MenuVisitDevastator2Homepage", "MenuVisitMAMu_Homepage", "MenuVisitCatlistHomepage"})
        if (QAction* a = menus_.action(n)) a->setVisible(false);
    auto openDoc = [this](const QString& file) {
        QString path = QDir(fe_.dataDir()).filePath("docs/" + file);
        if (QFileInfo::exists(path)) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        else QMessageBox::information(this, tr("Help"), tr("File not found:\n%1").arg(QDir::toNativeSeparators(path)));
    };
    menus_.on("MenuHelpViewQuickSetupGuide", [openDoc] { openDoc("el-quick-setup-quide.txt"); });
    menus_.on("MenuHelpViewReadme", [openDoc] { openDoc("el-readme.txt"); });
    menus_.on("MenuHelpViewFAQ", [openDoc] { openDoc("el-faq.txt"); });
    if (QAction* before = menus_.action("MenuHelpViewQuickSetupGuide")) {
        auto* guide = new QAction(tr("EmuLoader ReLoaded User Guide"), before->parent());
        guide->setToolTip(tr("Features, operation and differences from the original EmuLoader"));
        guide->setShortcut(QKeySequence::HelpContents);
        guide->setShortcutContext(Qt::ApplicationShortcut);
        addAction(guide); // F1 works anywhere in the window
        connect(guide, &QAction::triggered, this, [openDoc] {
            // next to the program, else in the data folder's docs
            QString path = QDir(programFolder()).filePath("EmuLoader-ReLoaded-Guide.html");
            if (QFileInfo::exists(path)) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            else openDoc("EmuLoader-ReLoaded-Guide.html");
        });
        if (auto* m = qobject_cast<QMenu*>(before->parent())) m->insertAction(before, guide);
    }
    setupUpdateCheck(); // Help > Check for Updates, and the weekly check
    menus_.on("MenuAbout", [this] {
        AboutDialog::Input in;
        in.icon = QDir(fe_.dataDir()).filePath("EmuLoader.ico");
        auto emulator = [this](SystemId id) {
            if (fe_.emulatorFile(id).isEmpty()) return QString();
            const QString v = fe_.emulatorVersion(id);
            return v.isEmpty() ? Frontend::section(id) : v;
        };
        in.mame = emulator(SystemId::MAME);
        in.hbmame = emulator(SystemId::HBMAME);
        in.dataFolder = fe_.dataDir();
        in.settingsFolder = fe_.userDir();
        in.licensesFolder = QDir(programFolder()).filePath("licenses");
        in.openGuide = [this] { openGuide(); };
        in.checkForUpdates = [this] { checkForUpdates(true); };
        AboutDialog dlg(in, this);
        dlg.exec();
    });

    // Games list popup
    menus_.on("PopupPlayGameStandard", [this] { runGame(); });
    menus_.on("PopupSelectaRandomGame", [this] {
        int n = filter_->rowCount();
        if (n == 0) return;
        QModelIndex i = filter_->index(QRandomGenerator::global()->bounded(n), 0);
        list_->setCurrentIndex(i);
        gamesView()->scrollTo(i, QAbstractItemView::PositionAtCenter);
    });
    menus_.on("PopupAddToFavorites", [this] { changeFavorite(true); });
    menus_.on("PopupDeleteFromFavorites", [this] { changeFavorite(false); });
    // Select Game at Startup: the game selected when EmuLoader starts, instead of the last one
    menus_.on("PopupStartupGameSelectionUseCurrentSelection", [this] {
        const MameGame* g = current();
        if (!g) return;
        WinIni& ini = fe_.ini();
        ini.writeInteger(kStartupSection, "SystemID", int(sys_));
        ini.writeInteger(kStartupSection, "ROMIdentification", -1); // the original's icon index: not used
        ini.writeInteger(kStartupSection, "MediaType", -1);
        ini.writeInteger(kStartupSection, "MachineType", g->isSoftware() ? 1 : 0); // 0 = MAME, 1 = a console/computer game
        ini.writeString(kStartupSection, "GameTitle", g->title);
        ini.writeString(kStartupSection, "GameName", g->name);
        ini.writeString(kStartupSection, "SoftwareName", g->softwareList);
        ini.writeString(kStartupSection, "GameNameDetails", Frontend::section(sys_) + ": " + g->key());
    });
    menus_.on("PopupStartupGameSelectionClear", [this] { fe_.ini().eraseSection(kStartupSection); });
    menus_.on("PopupStartupGameSelectionGameSelected", [this] {
        if (fe_.ini().readInteger(kStartupSection, "SystemID", -1) == int(sys_))
            selectGameByName(startupGame());
    });
    if (QMenu* m = menus_.menu("PopupMenuGamesList"))
        if (auto* sub = m->findChild<QMenu*>("PopupSelectGameStartup"))
            connect(sub, &QMenu::aboutToShow, this, [this] {
                WinIni& ini = fe_.ini();
                const bool set = ini.readInteger(kStartupSection, "SystemID", -1) != -1;
                if (QAction* a = menus_.action("PopupStartupGameSelectionGameSelected")) {
                    a->setText(set ? ini.readString(kStartupSection, "GameTitle").replace('&', "&&") : tr("(none)"));
                    a->setToolTip(set ? ini.readString(kStartupSection, "GameNameDetails") : tr("Use last selected game"));
                    a->setEnabled(set);
                }
                if (QAction* a = menus_.action("PopupStartupGameSelectionUseCurrentSelection")) {
                    const MameGame* g = current();
                    a->setText(tr("Use Current: %1").arg(g ? QString(g->title).replace('&', "&&") : QString()));
                    a->setToolTip(g ? Frontend::section(sys_) + ": " + g->key() : QString());
                    a->setEnabled(g);
                }
                if (QAction* a = menus_.action("PopupStartupGameSelectionClear")) a->setEnabled(set);
            });
    // Games List > Alternate Title Format [Game, The] and Game Title Extra Info: how titles are shown
    if (QAction* a = menus_.action("NewTitleFormat")) a->setChecked(fe_.ini().readBool("Preferences", "NewTitleFormat", false));
    menus_.on("NewTitleFormat", [this] {
        QAction* a = menus_.action("NewTitleFormat");
        fe_.ini().writeInteger("Preferences", "NewTitleFormat", a && a->isChecked() ? 1 : 0);
        applyPreferences();
        afterFilterChange();
    });
    {
        const char* extra[] = {"MenuGameTitleExtraInfo_FullTitle", "MenuGameTitleExtraInfo_RemoveFromParentSets",
                               "MenuGameTitleExtraInfo_RemoveFromCloneSets", "MenuGameTitleExtraInfo_RemoveFromAllSets"};
        const int now = std::clamp(fe_.ini().readInteger("Preferences", "TitleRemoveExtraText", 0), 0, 3);
        for (int i = 0; i < 4; ++i) {
            if (QAction* a = menus_.action(extra[i])) a->setChecked(i == now);
            menus_.on(extra[i], [this, i] {
                if (fe_.ini().readInteger("Preferences", "TitleRemoveExtraText", 0) == i) return;
                fe_.ini().writeInteger("Preferences", "TitleRemoveExtraText", i);
                applyPreferences();
                afterFilterChange();
            });
        }
    }
    // Games List > Incremental Search: by title (0) or by set name (1)
    {
        const int now = fe_.ini().readInteger("Preferences", "IncrementalSearch", 0);
        const char* items[] = {"MenuIncrementalSearchGameTitle", "MenuIncrementalSearchGameName"};
        for (int i = 0; i < 2; ++i) {
            if (QAction* a = menus_.action(items[i])) a->setChecked(i == now);
            menus_.on(items[i], [this, i] { fe_.ini().writeInteger("Preferences", "IncrementalSearch", i); });
        }
    }
    // Games List > Tool Bar Buttons > Show Tool Bar (Ctrl+T) and Bound to Games Panel; Show Games
    // Status Bar. Without the tool bar, the games list's menu has Main Menu and Show Tool Bar.
    {
        WinIni& ini = fe_.ini();
        auto setChecked = [this](const char* n, bool on) { if (QAction* a = menus_.action(n)) a->setChecked(on); };
        setChecked("MenuEnableToolBar", ini.readBool("ToolBar", "GamesFilters_Visible", true));
        setChecked("MenuBoundToGamesPanel", ini.readBool("ToolBar", "GamesFilters_BoundToGamesPanel", false));
        setChecked("MenuShowGamesStatusBar", ini.readBool("Preferences", "ShowGamesStatusBar", true));
        if (QAction* a = menus_.action("MenuBoundToGamesPanel")) a->setCheckable(true);
        menus_.on("MenuEnableToolBar", [this] { applyToolBarPlacement(); });
        menus_.on("PopupShowToolBar", [this] {
            if (QAction* a = menus_.action("MenuEnableToolBar")) a->setChecked(true);
            applyToolBarPlacement();
        });
        menus_.on("PopupMainMenu", [this] {
            if (QMenu* m = menus_.menu("MainMenuOptions")) m->exec(list_->mapToGlobal(QPoint(2, 2)));
        });
        menus_.on("MenuBoundToGamesPanel", [this] { applyToolBarPlacement(); });
        menus_.on("MenuShowGamesStatusBar", [this] {
            QAction* a = menus_.action("MenuShowGamesStatusBar");
            ui_->StatusBarPanel->setVisible(!a || a->isChecked());
            fe_.ini().writeInteger("Preferences", "ShowGamesStatusBar", !a || a->isChecked() ? 1 : 0);
            vcl::relayout(ui_->StatusBarPanel->parentWidget());
            updateStatus();
        });
        ui_->StatusBarPanel->setVisible(ini.readBool("Preferences", "ShowGamesStatusBar", true));
    }
    // Restore Default Screen Size/Position, Restore Default Screenshots Panel Size, and the columns'
    // Reset to Default Size/Position (all columns shown, at their default widths, in their order)
    menus_.on("MenuRestoreMainScreenDefaultScreenSizePosition", [this] {
        if (windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized)) return;
        resize(1130, 570);
        setScreenshotsWidth(415);
        if (QScreen* s = screen()) move(s->availableGeometry().center() - rect().center());
    });
    menus_.on("MenuRestoreDefaultScreenshotsPanelSize", [this] { setScreenshotsWidth(370); });
    // Customize Columns (PopupCustomizeColumnsClick), and the header's font
    for (const char* n : {"MenuCustomizeColumns", "PopupCustomizeColumns"})
        menus_.on(n, [this] {
            ColumnsEditorDialog::Input in;
            in.header = list_->header();
            in.customizeFont = [this] { customizeHeaderFont(false); };
            in.defaultFont = [this] { customizeHeaderFont(true); };
            ColumnsEditorDialog dlg(in, this);
            if (dlg.exec() == QDialog::Accepted) saveColumns();
        });
    for (const char* n : {"MenuCustomizeColumnsHeaderFont", "PopupCustomizeColumnsHeaderFont"})
        menus_.on(n, [this] { customizeHeaderFont(false); });
    for (const char* n : {"MenuDefaultColumnsHeaderFont", "PopupDefaultHeaderFont"})
        menus_.on(n, [this] { customizeHeaderFont(true); });
    for (const char* n : {"MenuResetColumnstoDefaultSizePosition", "PopupResetColumnsToDefault"})
        menus_.on(n, [this] {
            QHeaderView* h = list_->header();
            for (int c = 0; c < GamesModel::ColumnCount; ++c) {
                h->setSectionHidden(c, false);
                h->resizeSection(c, GamesModel::defaultWidth(c));
                h->moveSection(h->visualIndex(c), c);
            }
        });
    // Run Game Confirmation Dialog (checked by default in the menu's design, off until it's chosen,
    // as the original reads it: [Preferences] ShowEmulatorFullCommandLine, 0)
    if (QAction* a = menus_.action("MenuViewEmulatorFullCommandLine")) {
        a->setCheckable(true);
        a->setChecked(fe_.ini().readBool("Preferences", "ShowEmulatorFullCommandLine", false));
    }
    menus_.on("MenuViewEmulatorFullCommandLine", [this] {
        QAction* a = menus_.action("MenuViewEmulatorFullCommandLine");
        fe_.ini().writeInteger("Preferences", "ShowEmulatorFullCommandLine", a && a->isChecked() ? 1 : 0);
    });
    // Show Favorite Icon in Games (Favorites button menu): a star before favorites in the list
    if (QAction* a = menus_.action("MenuShowFavoriteIconInGamesList")) {
        a->setCheckable(true);
        a->setChecked(fe_.ini().readBool("Preferences", "ShowFavIconInGame", false));
    }
    menus_.on("MenuShowFavoriteIconInGamesList", [this] {
        QAction* a = menus_.action("MenuShowFavoriteIconInGamesList");
        fe_.ini().writeInteger("Preferences", "ShowFavIconInGame", a && a->isChecked() ? 1 : 0);
        applyPreferences();
        afterFilterChange();
    });
    // Delete Selected Games From Games List: in the original, for console/computer/handheld games
    // only (their lists are EmuLoader's own); MAME's games are always listed, as there.
    menus_.on("PopupDeleteSelectedGamesFromGamesList", [this] {
        QMessageBox::information(this, tr("Delete Selected Games From Games List"),
                                 tr("No valid games were found. MAME and arcade games are not supported, only "
                                    "console/computer/handheld games.\nAborting..."));
    });
    // Customize Splash Screen (MenuCustomizeSplashScreenClick)
    menus_.on("MenuCustomizeSplashScreen", [this] {
        openSettings("Splash Screen");
    });
    // Arcade ROMs Folders (MenuROMsFoldersClick)
    menus_.on("MenuROMsFolders", [this] {
        RomsFoldersDialog dlg(fe_, sys_, this);
        dlg.exec();
    });
    // Arcade Files Versions (MenuViewFilesVersionsClick)
    menus_.on("MenuViewFilesVersions", [this] {
        FileVersionsDialog dlg(fe_, this);
        dlg.exec();
    });
    // Export MAME and Arcade Games List (MenuExportGamesListTextFileClick)
    menus_.on("MenuExportGamesListTextFile", [this] {
        ExportGamesDialog dlg({model_, filter_, list_, sys_}, extrasIni(), this);
        dlg.exec();
    });
    // Scan MAME/HBMAME Missing Audio Samples (MenuScanMAMEAudioSamplesClick)
    menus_.on("MenuScanMAMEAudioSamples", [this] {
        ScanSamplesDialog::Input in;
        in.fe = &fe_;
        in.model = model_;
        in.system = sys_;
        in.play = [this](const QString& name) { // PopupPlayGameClick
            if (!selectGameByName(name)) {
                QMessageBox::warning(QApplication::activeWindow(), tr("Play"),
                                     tr("The game isn't shown in the main games list, because of the filters or the "
                                        "search. Show it there first."));
                return;
            }
            runGame();
        };
        ScanSamplesDialog dlg(in, this);
        if (dlg.scan()) dlg.exec();
    });
    // Use Alternate Frontend Icons ([Preferences] UseAlternateFrontendIcons): the main_icons\alternate folder
    if (QAction* a = menus_.action("MenuUseAlternateFrontendIcons")) {
        a->setCheckable(true);
        a->setChecked(fe_.ini().readBool("Preferences", "UseAlternateFrontendIcons", false));
    }
    menus_.on("MenuUseAlternateFrontendIcons", [this] {
        QAction* a = menus_.action("MenuUseAlternateFrontendIcons");
        fe_.ini().writeInteger("Preferences", "UseAlternateFrontendIcons", a && a->isChecked() ? 1 : 0);
        setToolBarIconSize(fe_.ini().readInteger("ToolBar", "GamesFilters_IconSize", 1)); // the tool bar's pictures again
        listDelegate_->setFavoriteIcon(QIcon(fe_.mainIcon("favorite_game.ico")));
        list_->viewport()->update();
        iconView_->setFavoriteIcon(QIcon(fe_.mainIcon("favorite_game.ico")));
        iconView_->setSpecialIcon(QIcon(fe_.mainIcon("special.ico")));
    });
    // MAMu_ icons (PopupEnableMAMu_IconsClick and the options under it), EmuLoaderRL.ini [MAMu_]
    {
        MamuIcons::Options o;
        o.load(fe_.ini());
        mamu_.setOptions(o);
        if (o.enabled) mamu_.setFolders(fe_.iconFolders());
        // MAMu_ icons are the machines' (a software list game of the same name isn't the machine)
        model_->customIcon = [this](const MameGame& g) { return g.isSoftware() ? QIcon() : mamu_.icon(g); };
        struct Opt { const char* item; bool MamuIcons::Options::*member; };
        static const Opt opts[] = {{"PopupEnableMAMu_Icons", &MamuIcons::Options::enabled},
                                   {"PopupShowParentIcons", &MamuIcons::Options::parent},
                                   {"PopupShowCloneIcons", &MamuIcons::Options::clone},
                                   {"PopupEnableDeviceSetCustomIcon", &MamuIcons::Options::deviceSet},
                                   {"PopupShowGameBiosIcons", &MamuIcons::Options::bios},
                                   {"PopupShowGameDriverIcons", &MamuIcons::Options::driver},
                                   {"PopupForceGameBiosIcons", &MamuIcons::Options::forceBios},
                                   {"PopupForceGameDriverIcons", &MamuIcons::Options::forceDriver}};
        for (const Opt& op : opts) {
            if (QAction* a = menus_.action(op.item)) {
                a->setCheckable(true);
                a->setChecked(o.*(op.member));
            }
            menus_.on(op.item, [this, op] {
                QAction* a = menus_.action(op.item);
                MamuIcons::Options n = mamu_.options();
                n.*(op.member) = a && a->isChecked();
                const bool turnedOn = n.enabled && !mamu_.options().enabled;
                mamu_.setOptions(n);
                if (turnedOn) mamu_.setFolders(fe_.iconFolders());
                n.save(fe_.ini());
                ui_->ButtonMAMu_Icons->setChecked(n.enabled);
                refreshIcons();
            });
        }
        ui_->ButtonMAMu_Icons->setChecked(o.enabled);
        menus_.on("PopupReloadMAMu_Icons", [this] {
            mamu_.setFolders(fe_.iconFolders());
            refreshIcons();
        });
        menus_.on("PopupSetFoldersMAMu_", [this] { openSettings({}, "MAMu_IconsFolderBox"); });
        menus_.on("PopupMAMu_IconsManager", [this] { openImagesManager(ImageFinder::Icons); }); // the Icons category
    }
    // Night Mode (MenuEnableNightModeClick) and its colors
    if (QAction* a = menus_.action("MenuEnableNightMode")) {
        a->setCheckable(true);
        a->setChecked(night_);
    }
    menus_.on("MenuEnableNightMode", [this] {
        QAction* a = menus_.action("MenuEnableNightMode");
        applyNight(a && a->isChecked());
    });
    menus_.on("MenuCustomizeNightModeColors", [this] { openSettings("Night Mode"); });
    // Games List Appearance: the light mode colors (the original's Preferences page 2)
    menus_.on("MenuCustomizeGamesListAppearance", [this] { openSettings("Colors"); });
    // Show Players Info From nplayers.ini Only ([Preferences] PlayersInfoFromIniOnly)
    if (QAction* a = menus_.action("MenuShowPlayersInfoFromNplayersIniOnly")) {
        a->setCheckable(true);
        a->setChecked(fe_.ini().readBool("Preferences", "PlayersInfoFromIniOnly", false));
    }
    menus_.on("MenuShowPlayersInfoFromNplayersIniOnly", [this] {
        QAction* a = menus_.action("MenuShowPlayersInfoFromNplayersIniOnly");
        fe_.ini().writeInteger("Preferences", "PlayersInfoFromIniOnly", a && a->isChecked() ? 1 : 0);
        applyPreferences();
    });
    menus_.on("PopupAddToSpecialList", [this] { changeSpecial(true); });
    menus_.on("PopupRemoveFromSpecialList", [this] { changeSpecial(false); });
    // Scan Games (Audit ROMs), Select Scan Mode, View Missing ROMs/CHDs (MainWindowAudit.cpp)
    // One item that opens the Scan Games window, in place of the original's Select Scan Mode and
    // the Scan Games submenu (whose items used the mode chosen before)
    if (QAction* a = menus_.action("PopupScanGames")) {
        if (QMenu* m = a->menu()) {
            for (QAction* c : m->actions()) c->setVisible(false);
            a->setMenu(static_cast<QMenu*>(nullptr));
        }
        a->setText(tr("Scan Games (Audit ROMs)..."));
        a->setToolTip(tr("Check which games you have: all of them, the available or missing ones, or the selected ones"));
    }
    menus_.on("PopupScanGames", [this] { openScanGames(); });
    menus_.on("PopupScanResultsAllGames", [this] { viewScanResults(false); });
    menus_.on("PopupScanResultsSelectedGame", [this] { viewScanResults(true); });

    // Custom settings: one file of MAME's ini chain (TFormMain.SetCustomOptions).
    menus_.on("PopupSetCustomOptions", [this] { openCustomSettings(12, false); });
    menus_.on("PopupDeleteCustomOptions", [this] { openCustomSettings(12, true); });
    menus_.on("PopupSetGameDriverCustomOptions", [this] { openCustomSettings(9, false); });
    menus_.on("PopupDeleteGameDriverCustomOptions", [this] { openCustomSettings(9, true); });
    struct Extra { const char* set; const char* del; int level; std::function<void(MameGame&)> adjust; };
    const Extra extras[] = {
        {"PopupSetMAMEHorizontalGamesOptions", "PopupDeleteMAMEHorizontalGamesOptions", 3, [](MameGame& g) { g.orientation = 0; }},
        {"PopupSetMAMEVerticalGamesOptions", "PopupDeleteMAMEVerticalGamesOptions", 2, [](MameGame& g) { g.orientation = 1; }},
        {"PopupSetDebugCustomOptions", "PopupDeleteDebugCustomOptions", 1, {}},
        {"PopupSetMAMERasterGameOptions", "PopupDeleteMAMERasterGameOptions", 8, [](MameGame& g) { g.screenType = 0; }},
        {"PopupSetMAMEVectorGamesOptions", "PopupDeleteMAMEVectorGamesOptions", 8, [](MameGame& g) { g.screenType = 1; }},
        {"PopupSetMAMELCDGameOptions", "PopupDeleteMAMELCDGameOptions", 8, [](MameGame& g) { g.screenType = 2; }},
        {"PopupSetMAMESVGGameOptions", "PopupDeleteMAMESVGGameOptions", 8, [](MameGame& g) { g.screenType = 3; }},
        {"PopupSetArcadeIniCustomOptions", "PopupDeleteArcadeIniCustomOptions", 4, [](MameGame& g) { g.systemType = 0; }},
        {"PopupSetConsoleIniCustomOptions", "PopupDeleteConsoleIniCustomOptions", 5, [](MameGame& g) { g.systemType = 1; }},
        {"PopupSetComputerIniCustomOptions", "PopupDeleteComputerIniCustomOptions", 6, [](MameGame& g) { g.systemType = 1; }},
        {"PopupSetOtherSysIniCustomOptions", "PopupDeleteOtherSysIniCustomOptions", 7, [](MameGame& g) { g.systemType = 1; }},
    };
    for (const Extra& e : extras) {
        int level = e.level;
        auto adjust = e.adjust;
        menus_.on(e.set, [this, level, adjust] { openCustomSettings(level, false, adjust); });
        menus_.on(e.del, [this, level, adjust] { openCustomSettings(level, true, adjust); });
    }

    // Browse Games With: the keyboard and mouse only. The original's modes for arcade controllers
    // (Devastator II, Hot Rod SE, SlikStik, X-Arcade) and their layout pictures are left out: such
    // controllers are set up as a keyboard today.
    if (QAction* a = menus_.action("MenuArcadeControlKeyboardMouse")) a->setChecked(true);
    menus_.on("MenuArcadeControlKeyboardMouse", [this] {
        if (QAction* a = menus_.action("MenuArcadeControlKeyboardMouse")) a->setChecked(true);
    });
    if (QAction* sub = menus_.action("MenuArcadeBrowseGames"); sub && sub->menu())
        for (QAction* a : sub->menu()->actions())
            if (a->isSeparator()) a->setVisible(false);

    // night mode's color boxes show these and do the copying (NightModeDialog)
    menus_.on("PopupNightModeCopyColor", [] {});
    menus_.on("PopupNightModePasteColor", [] {});

    // Console/computer systems and other emulators are outside this port's scope.
    menus_.disableUnhandled({"MenuConsoleComputer", "PopupRunConsoleComputerGameWithMAME",
                             "PopupPlayGameWithCustomSystem",
                             "PopupEnableAutoRunCustomGamesWithMAME", "CreateSupermodelXMLdatFromSourceFile1",
                             "MenuArcadeControlDevastator2", "MenuArcadeControlHotRodSE", "MenuArcadeControlSlikStik",
                             "MenuArcadeControlXArcade", "MenuArcadeControlSlikStik_SwapStick",
                             "MenuArcadeControllersLayout",
                             // the tool bar's Parameters button opens the game's parameters instead
                             "MenuParametersCustomSystemTitle", "MenuParametersSystemSeparator",
                             "PopupUseCustomEmuParameter1", "PopupUseCustomEmuParameter2",
                             "getsnaps1", "MenuValidateMAMECHDTypes", "Menu4KModeDisable", "Menu4KModeReadMe",
                             // hidden in the original too
                             "PopupEditDriverCustomParameters", "PopupDeleteDriverCustomParameters",
                             // in the Scan Games window instead
                             "PopupSelectScanGamesMode", "PopupScanAllGames", "PopupScanAvailableGames", "PopupScanMissingGames",
                             "PopupScanMissingROMsCHDs", "PopupScanAllSelectedGames", "PopupScanForceSelectedGamesAvailable"});

    // Find a Setting (new): first item of Menu Options, and Ctrl+Shift+F.
    if (QMenu* m = menus_.menu("MainMenuOptions")) {
        auto* find = new QAction(tr("Find a Setting..."), m);
        find->setObjectName("MenuFindSetting");
        find->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
        find->setShortcutContext(Qt::WindowShortcut);
        find->setToolTip(tr("Search the settings of every window and menu by name, and go to the one you pick"));
        connect(find, &QAction::triggered, this, [this] { findSetting(); });
        QAction* first = m->actions().value(0);
        m->insertAction(first, find);
        m->insertSeparator(first);
        addAction(find); // the shortcut works without opening the menu
    }
}

// The menus Find a Setting can open, with what to call them and where they open.
QList<MainWindow::MenuPlace> MainWindow::menuPlaces() const {
    QList<MenuPlace> out;
    auto below = [](QWidget* w) { return w->mapToGlobal(QPoint(0, w->height())); };
    out.append({"MainMenuOptions", tr("Menu"), below(ui_->ButtonMainMenuOptions)});
    if (list_) out.append({"PopupMenuGamesList", tr("Games list right-click menu"), list_->viewport()->mapToGlobal(QPoint(60, 40))});
    if (ImageView* v = layouts_ ? layouts_->view(1) : nullptr)
        out.append({"PopupMenuImages", tr("Image right-click menu"), v->mapToGlobal(QPoint(40, 40))});
    out.append({"PopupFilterGameTitle", tr("Search box settings menu"), below(ui_->ButtonFilterTitleSettings_ToolBar)});
    ElButton* fav = ui_->ButtonGameFilterFavorites;
    out.append({"PopupFavorites", tr("%1 button, right-click menu").arg(fav->caption()), below(fav)});
    for (ElButton* b : ui_->ToolBarButtons->findChildren<ElButton*>()) {
        auto* m = qobject_cast<QMenu*>(b->property("elMenu").value<QObject*>());
        if (!m || m->objectName().isEmpty() || !menus_.menu(m->objectName())) continue;
        if (std::any_of(out.begin(), out.end(), [m](const MenuPlace& p) { return p.name == m->objectName(); })) continue;
        QWidget* at = b->property("elOverflow").toBool() || b->isHidden() ? static_cast<QWidget*>(overflow_) : b;
        out.append({m->objectName(), tr("%1 button menu").arg(b->caption()), at ? below(at) : QCursor::pos()});
    }
    return out;
}

// Menu Options > Find a Setting: searches the Settings window's General, Games List, Game Info
// and Documents and Colors tabs, MAME settings and the menus.
void MainWindow::findSetting(const QString& query, int row) {
    QList<SettingEntry> entries;
    {
        PreferencesDialog prefs(fe_, this);
        prefs.takePages();
        prefs.addSettingsTo(entries);
    }
    if (QFileInfo::exists(fe_.mameIniPath(sys_))) {
        MameSettingsDialog mame(fe_, sys_, this);
        mame.addSettingsTo(entries);
    }
    for (const MenuPlace& p : menuPlaces())
        if (QMenu* m = menus_.menu(p.name)) SettingsSearch::addMenu(entries, m, p.name, p.title);
    FindSettingDialog dlg(entries, query, this);
    if (row >= 0) dlg.selectRow(row);
    if (dlg.exec() != QDialog::Accepted) return;
    const SettingEntry e = dlg.chosen();
    if (e.window == "preferences") {
        openSettings({}, e.target);
    } else if (e.window == "mame") {
        openEmulatorSettings(e.target);
    } else if (e.window == "menu") {
        QString menu = e.target.section('/', 0, 0), action = e.target.section('/', 1);
        for (const MenuPlace& p : menuPlaces())
            if (p.name == menu)
                if (QMenu* m = menus_.menu(menu))
                    if (QAction* a = m->findChild<QAction*>(action))
                        // a click elsewhere closed the menus: Find a Setting again, as it was
                        SettingsSearch::revealInMenu(m, a, p.at, [this, q = dlg.query(), r = dlg.chosenRow()] {
                            findSetting(q, r);
                        });
    }
}

void MainWindow::openFontSettings() {
    GameFontsDialog dlg(model_->fonts(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    model_->setFonts(dlg.fonts());
    list_->setFont(ScriptFonts::shown(dlg.fonts().allSystems));
    if (!dlg.fonts().save(gameFontsFile()))
        FileProblem::cantSave(this, tr("Customize Game Fonts"), gameFontsFile());
}

// ---------------------------------------------------------------- list

// Game Icon Size (PopupGameIconSize): icon and row height as ELV_CellSize in uMain.pas.
void MainWindow::setGameIconSize(int idx, bool save) {
    static const int icon[5] = {68, 48, 32, 24, 16}, row[5] = {72, 52, 36, 28, 22};
    if (idx < 0 || idx > 4) idx = 3;
    gameIconSize_ = idx;
    QString name = current() ? current()->key() : QString();
    QByteArray layout = list_->header()->saveState();
    list_->setIconSize(QSize(icon[idx], icon[idx]));
    // (a larger font - a language's FontSize= - gets the room it needs)
    int need = 0;
    if (ScriptFonts::sizeDelta() > 0)
        for (const QFont& f : model_->fonts().font) need = std::max(need, QFontMetrics(ScriptFonts::shown(f)).height() + 6);
    model_->setRowHeight(std::max(row[idx], need));
    list_->header()->restoreState(layout);
    if (!name.isEmpty()) {
        int r = db_.indexOf(name);
        if (r >= 0) {
            QModelIndex i = filter_->mapFromSource(model_->index(r, 0));
            list_->setCurrentIndex(i);
            gamesView()->scrollTo(i, QAbstractItemView::PositionAtCenter);
        }
    }
    const char* items[5] = {"PopupIconSizeMegaLarge", "PopupIconSizeExtraLarge", "PopupIconSizeLarge",
                            "PopupIconSizeStandard", "PopupIconSizeSmall"};
    for (int i = 0; i < 5; ++i)
        if (QAction* a = menus_.action(items[i])) { a->setCheckable(true); a->setChecked(i == idx); }
    if (save) fe_.ini().writeInteger("Preferences", "GameIconSize", idx);
}

void MainWindow::setSearchField(int field) {
    if (field < 0 || field >= GamesFilter::SearchFieldCount) field = GamesFilter::SearchTitle;
    searchField_ = field;
    for (int i = 0; i < GamesFilter::SearchFieldCount; ++i)
        if (QAction* a = menus_.action(kSearchItems[i])) {
            a->setCheckable(true);
            a->setChecked(i == field);
            if (i == field) {
                QLabel* l = ui_->LabelSearchGamesBy_ToolBar;
                l->setText(a->text().remove('&'));
                vcl::place(l, QRect(l->pos(), QSize(l->sizeHint().width(), l->height())));
                layoutSearchPanel(); // the small tool bar puts the text box after the label
            }
        }
}

// FilterSearchBarHelpClick
void MainWindow::searchHelp() {
    QMessageBox box(this);
    box.setWindowTitle(tr("How to use the search bar"));
    box.setIcon(QMessageBox::Information);
    box.setTextFormat(Qt::RichText);
    box.setText(tr(
        "<p><b>1.</b> Type what you want to search for in the text box and press <b>Enter</b> or click the "
        "<b>Apply</b> button.</p>"
        "<p><b>2.</b> You can search by something else: click the settings button and choose it. Some need a file in "
        "<code>arcade\\filters</code>: <code>catver.ini</code> for the category and the version added, "
        "<code>languages.ini</code> for the language; <code>nplayers.ini</code> is optional for the number of "
        "players.</p>"
        "<p><b>3.</b> Click the <b>Reset</b> button to clear the search and go back to the tool bar filters.</p>"
        "<p><b>4.</b> Check <b>Include Tool Bar Filters</b> to search only the games the tool bar filters show "
        "(Parent/Clone, Have/Miss, Driver Status, Misc Filters...).</p>"
        "<p><b>Note</b>: if you change the tool bar filters, apply the search again.</p>"));
    box.exec();
}

// ApplyFilterSearchBar
void MainWindow::applySearch() {
    QLineEdit* ed = ui_->FilterGameTitle_ToolBar;
    if (ed->text().isEmpty()) { // BlinkBkEdit: flash the empty box
        ed->setStyleSheet("QLineEdit { background: #ffc8c8; }");
        QTimer::singleShot(250, ed, [ed] { ed->setStyleSheet({}); });
        return;
    }
    QAction* inc = menus_.action("FilterTextBarIncludeToolBarFilters");
    rememberSelection();
    filter_->setSearch(ed->text(), searchField_, !inc || inc->isChecked());
    afterFilterChange();
}

// After the list was filtered again: keep the selected game if it is still listed, else select the first.
void MainWindow::afterFilterChange() {
    DiagnosticLog::write("filters", QString("ApplyFilters completed; filter=%1; visible=%2").arg(filter_->arcadeFilter()).arg(filter_->rowCount()));
    updateStatus();
    QModelIndex sel;
    if (!selectedBeforeFilter_.isEmpty()) {
        int r = db_.indexOf(selectedBeforeFilter_);
        if (r >= 0) sel = filter_->mapFromSource(model_->index(r, 0));
    }
    if (!sel.isValid() && filter_->rowCount() > 0) sel = filter_->index(0, 0);
    if (sel.isValid()) {
        list_->selectionModel()->setCurrentIndex(sel, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        gamesView()->scrollTo(sel, QAbstractItemView::PositionAtCenter);
    }
    updateStatus();
}

// Preferences > Hide Command Prompt Box. A GUI program starts MAME without a console window;
// when the box isn't to be hidden MAME gets its own console, showing its messages.
// readOutput: the output is read here (-listxml), so the console stays empty, as in the
// original where it went to a file.
void commandPromptBox(QProcess* p, bool hide, bool readOutput) {
#ifdef Q_OS_WIN
    if (hide) return;
    p->setCreateProcessArgumentsModifier([readOutput](QProcess::CreateProcessArguments* a) {
        a->flags &= ~CREATE_NO_WINDOW;
        a->flags |= CREATE_NEW_CONSOLE;
        if (!readOutput) {
            a->startupInfo->dwFlags &= ~STARTF_USESTDHANDLES; // MAME writes to that console
            a->inheritHandles = FALSE;
        }
    });
#else
    Q_UNUSED(p);
    Q_UNUSED(hide);
    Q_UNUSED(readOutput);
#endif
}

// Machines button (ButtonGameFilterMachineTypeSystemsMultiClick)
// Miscellaneous Filters (ButtonExtraFiltersClick): saved in ini_files\games_filters.ini
// [Miscellaneous]; the list is filtered again when they change.
// arcade\filters\mame_control_type.ini (LoadControlTypeList): name=title
QList<QPair<QString, QString>> MainWindow::controlTypes() const {
    QList<QPair<QString, QString>> controls;
    QString text;
    if (readTextFile(QDir(fe_.dataDir()).filePath("arcade/filters/mame_control_type.ini"), text))
        for (const QString& line : text.split('\n')) {
            int eq = line.indexOf('=');
            if (eq > 0 && !line.startsWith(';')) controls << qMakePair(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
        }
    return controls;
}

// The search bar's Controls menu (PopupSearchBarControlsFilter): Include Tool Bar Filters
// ([ListFilter] SearchBarControlsIncludeMainFilters), then the control types; one picked shows
// the games with it, its name in the search box (PopupSearchBarControlsFilterSelectionClick).
void MainWindow::showControlsFilterMenu() {
    const QList<QPair<QString, QString>> controls = controlTypes();
    QMenu menu(this);
    QAction* inc = menu.addAction(tr("Include Tool Bar Filters"));
    inc->setCheckable(true);
    inc->setChecked(fe_.ini().readInteger("ListFilter", "SearchBarControlsIncludeMainFilters", 1) != 0);
    connect(inc, &QAction::toggled, this, [this](bool on) {
        fe_.ini().writeInteger("ListFilter", "SearchBarControlsIncludeMainFilters", on ? 1 : 0);
        fe_.saveIni();
    });
    menu.addSection(tr("Filter games by..."));
    if (controls.isEmpty()) menu.addAction(tr("(mame_control_type.ini was not found)"))->setEnabled(false);
    for (const auto& [name, title] : controls) {
        QAction* a = menu.addAction(title);
        a->setCheckable(true);
        a->setChecked(filter_->controlSearch() == name);
        a->setData(name);
    }
    QWidget* b = ui_->ButtonFilterControls_ToolBar;
    QAction* picked = menu.exec(b->mapToGlobal(QPoint(0, b->height())));
    if (!picked || picked == inc || !picked->data().isValid()) return;
    filterByControl(picked->data().toString());
}

void MainWindow::filterByControl(const QString& control) {
    ui_->FilterGameTitle_ToolBar->setText(control);
    rememberSelection();
    filter_->setControlSearch(control, fe_.ini().readInteger("ListFilter", "SearchBarControlsIncludeMainFilters", 1) != 0);
    afterFilterChange();
}

void MainWindow::openMiscFilters() {
    const QList<QPair<QString, QString>> controls = controlTypes();
    bool romsKnown = false;
    for (const MameGame& g : db_.games())
        if (g.romFlag(MameGame::RomsKnown)) { romsKnown = true; break; }
    MiscFiltersDialog dlg(filter_->miscFilters(), controls, romsKnown || db_.games().isEmpty(), this);
    dlg.move(popupPos(ui_->ButtonExtraFilters, dlg.size()));
    if (dlg.exec() != QDialog::Accepted) return;
    MiscFilters f = dlg.values();
    f.save(fe_.gamesFiltersFile());
    if (f.mainCpu != filter_->miscFilters().mainCpu) { // Filter MAME Games by Main CPU: the CPU row
        MiscFilters g = filter_->miscFilters();
        g.mainCpu = f.mainCpu;
        filter_->setMiscFilters(g, multiSlot_);
        showCpuFilter(f.mainCpu);
    }
    if (f == filter_->miscFilters()) return;
    rememberSelection();
    filter_->clearSearch();
    filter_->setMiscFilters(f, multiSlot_);
    updateFilterIcons();
    afterFilterChange();
}

// Games Filters - MAME and Arcade (ButtonArcadeGamesFiltersClick): arcade\filters\mame_filters.ini.
// Picking one while the favorites list is shown goes back to the whole list, as in the original.
void MainWindow::openGamesFilters() {
    QList<GamesFiltersDialog::Filter> filters;
    QString err;
    if (!GamesFiltersDialog::readFilters(QDir(fe_.dataDir()).filePath("arcade/filters/mame_filters.ini"), filters, &err)) {
        QMessageBox::warning(this, tr("Games Filters - MAME and Arcade"), err);
        return;
    }
    int size = fe_.ini().readInteger("ListFilter", "ArcadeGames_IconSize", 2);
    GamesFiltersDialog dlg(filters, filter_->arcadeFilter(), fe_.resourcesFolder() + "main_icons/arcade_filters/", size, this);
    dlg.move(popupPos(ui_->ButtonArcadeGamesFilters, dlg.size()));
    int r = dlg.exec();
    fe_.ini().writeInteger("ListFilter", "ArcadeGames_IconSize", dlg.iconSize());
    QString sel = dlg.selected();
    if (r != QDialog::Accepted || sel.isEmpty() || (sel == filter_->arcadeFilter() && !filter_->favoritesOnly())) return;
    rememberSelection();
    filter_->clearSearch();
    if (filter_->favoritesOnly()) {
        filter_->setFavoritesOnly(false);
        if (QAction* a = menus_.action("PopupEnableFavorites")) a->setChecked(false);
    }
    filter_->setArcadeFilter(sel);
    updateFilterIcons();
    afterFilterChange();
}

void MainWindow::loadGameFilters() {
    const QString base = Frontend::fileBase(sys_);
    multiSlot_ = MiscFilters::loadMultiSlot(MultiSlot::cartFile(fe_.gamesFolder(), base, "neogeo"),
                                            MultiSlot::cartFile(fe_.gamesFolder(), base, "stv"));
    GamesFiltersDialog::readFilters(QDir(fe_.dataDir()).filePath("arcade/filters/mame_filters.ini"), arcadeFilters_, nullptr);
    filter_->setMiscFilters(MiscFilters::load(fe_.gamesFiltersFile()), multiSlot_);
}

QString MainWindow::arcadeFilterTitle(const QString& name) const {
    for (const auto& f : arcadeFilters_)
        if (f.name == name) return f.group.isEmpty() ? f.description : f.group + " -> " + f.description;
    return name;
}

void MainWindow::openMachineFilter() {
    MachineFilterDialog dlg(filter_->machineTypes(), fe_.resourcesFolder() + "main_icons", this);
    QPoint at = ui_->ButtonGameFilterMachineTypeSystemsMulti->mapToGlobal(QPoint(10, ui_->ButtonGameFilterMachineTypeSystemsMulti->height() + 1));
    dlg.move(at);
    if (dlg.exec() != QDialog::Accepted) return;
    rememberSelection();
    filter_->clearSearch();
    filter_->setMachineTypes(dlg.machineTypes());
    MachineFilterDialog::save(fe_.gamesFiltersFile(), dlg.machineTypes());
    afterFilterChange();
    showMachinesPanel(); // Enable MAME Machines Filter Panel
}

// ---------------------------------------------------------------- Night Mode

QString MainWindow::nightFile() const {
    QString profile = fe_.ini().readString("Preferences", "NightModeProfile", "Default").trimmed();
    if (profile.isEmpty()) profile = "Default";
    return fe_.userFile("nightmode/" + profile + ".ini");
}

QString MainWindow::gameFontsFile() const {
    return night_ ? NightMode::fontsFile(fe_.iniFilesFolder()) : fe_.iniFilesFolder() + "sysgamecustomfont.ini";
}

// The colors in use: the night profile's (nightmode\<profile>.ini), or lightmode.ini's
LightMode MainWindow::loadLook() {
    if (night_) {
        nightMode_ = NightMode();
        nightMode_.load(nightFile());
        return nightMode_.look;
    }
    LightMode lm;
    lm.load(fe_.iniFilesFolder() + "lightmode.ini");
    return lm;
}

void MainWindow::saveLook(const std::function<void(LightMode&)>& change) {
    if (night_) {
        NightMode nm;
        nm.load(nightFile());
        change(nm.look);
        nm.save(nightFile());
        nightMode_ = nm;
        return;
    }
    LightMode lm;
    lm.load(fe_.iniFilesFolder() + "lightmode.ini");
    change(lm);
    lm.save(fe_.iniFilesFolder() + "lightmode.ini");
}

// The whole program dark, or light again (ToggleNightMode): the controls' palette (Fusion's, so the
// colors show on Windows too), the forms' own colors, the night profile's colors for the games
// list, tool bar, status bar, splitters, header and menus, and the night game fonts.
void MainWindow::applyNight(bool on, bool save) {
    night_ = on;
    if (save) fe_.ini().writeInteger("Preferences", "NightModeEnabled", on ? 1 : 0);
    if (QAction* a = menus_.action("MenuEnableNightMode")) {
        const QSignalBlocker b(a);
        a->setChecked(on);
    }
    const LightMode look = loadLook();
    showNight(on, look);
}

// Night Mode's colors on everything (also for Customize Night Mode Colors, before they're saved)
void MainWindow::showNight(bool on, const LightMode& look) {
    vcl::NightLook n;
    n.on = on;
    if (on) {
        n.panelTop = LightMode::qc(nightMode_.panelsGradient ? nightMode_.panelsTop : nightMode_.panelsBottom);
        n.panelBottom = LightMode::qc(nightMode_.panelsBottom);
        n.panelFrame = LightMode::qc(nightMode_.panelsOuterFrame);
    }
    vcl::setNight(n);
    QApplication::setPalette(nightPalette(on));
    if (on) {
        QApplication::setStyle("Fusion");
        const QColor frame = LightMode::qc(nightMode_.menuFrame);
        qApp->setStyleSheet(QString("QMenu { border: 1px solid %1; }"
                                    "QToolTip { color: %2; background-color: %3; border: 1px solid %4; }")
                                .arg(frame.lighter(250).name(), LightMode::qc(nightMode_.buttonFont).name(),
                                     QColor(32, 32, 32).name(), frame.lighter(250).name()));
    } else {
#ifdef Q_OS_WIN
        // Windows' own controls; drawn off screen they can't be (no window handles:
        // empty pictures, QPainter warnings, wrong sizes), so Fusion there, as on other systems
        QApplication::setStyle(QGuiApplication::platformName() == QLatin1String("offscreen") ? "Fusion" : "windowsvista");
#else
        QApplication::setStyle("Fusion");
#endif
        QApplication::setPalette(QApplication::style()->standardPalette());
        qApp->setStyleSheet(QString());
    }
    for (QWidget* w : QApplication::topLevelWidgets()) vcl::recolor(w);
    GameFonts f;
    f.setNight(on);
    f.load(gameFontsFile());
    model_->setFonts(f);
    list_->setFont(ScriptFonts::shown(f.allSystems));
    if (iconView_) iconView_->setFont(ScriptFonts::shown(f.allSystems));
    if (listDelegate_) listDelegate_->setGroupColors(on ? QColor(0xff, 0xac, 0x00) : QColor(0x1e, 0x39, 0x87),
                                                     on ? QColor(0x50, 0x50, 0x50) : QColor(0xa0, 0xb8, 0xdc));
    applyLightMode(look);
    list_->viewport()->update();
}

// Customize Night Mode Colors (MenuCustomizeNightModeColorsClick)
void MainWindow::addNightModePage(SettingsDialog& s, QVector<std::function<void()>>&) {
    NightModeDialog::Input in;
    in.folder = fe_.userFile("nightmode");
    in.profile = QFileInfo(nightFile()).completeBaseName();
    in.apply = [this](const QString& profile) {
        fe_.ini().writeString("Preferences", "NightModeProfile", profile);
        if (night_) applyNight(true, false);
    };
    in.preview = [this](const NightMode& nm) {
        if (!night_) return;
        nightMode_ = nm;
        showNight(true, nm.look);
    };
    in.colorMenu = menus_.menu("PopupNightModeCopyPasteColor");
    auto* dlg = new NightModeDialog(in, &s);
    s.addDialog(tr("Night Mode"), dlg, "", "");
}

void MainWindow::refreshIcons() {
    model_->refreshIcons();
    list_->viewport()->update();
    if (iconView_) iconView_->viewport()->update();
}

QPalette MainWindow::nightPalette(bool on) const {
    if (!on) return QApplication::style()->standardPalette();
    const NightMode& nm = nightMode_;
    const QColor text = LightMode::qc(nm.buttonFont);            // clCream
    const QColor window(30, 30, 30), base(LightMode::qc(nm.look.listBack).lighter(130));
    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, base.lighter(115));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, LightMode::qc(nm.buttonBottom));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, QColor(250, 80, 80));
    p.setColor(QPalette::Highlight, LightMode::qc(nm.look.selTop));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, QColor(32, 32, 32));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, QColor(128, 128, 128));
    p.setColor(QPalette::Link, QColor(110, 170, 255));
    p.setColor(QPalette::LinkVisited, QColor(170, 140, 255));
    p.setColor(QPalette::Light, QColor(80, 80, 80));
    p.setColor(QPalette::Midlight, QColor(60, 60, 60));
    p.setColor(QPalette::Mid, QColor(45, 45, 45));
    p.setColor(QPalette::Dark, QColor(15, 15, 15));
    p.setColor(QPalette::Shadow, Qt::black);
    for (QPalette::ColorRole r : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, r, LightMode::qc(nm.buttonDisabledFont));
    p.setColor(QPalette::Disabled, QPalette::Button, LightMode::qc(nm.buttonDisabledBottom));
    p.setColor(QPalette::Disabled, QPalette::Base, window);
    return p;
}

// The column titles' font (UpdateHeaderFont): [GamesListHeader] ColumnsHeaderFont* of lightmode.ini,
// at least the original's 23 pixels high.
void MainWindow::applyHeaderFont() {
    QHeaderView* h = list_->header();
    const QFont f = ScriptFonts::shown(lightMode_.headerFont()); // (+ the language's FontSize=)
    h->setFont(f);
    QPalette p = h->palette();
    p.setColor(QPalette::ButtonText, LightMode::qc(lightMode_.headerFontColor));
    h->setPalette(p);
    h->setFixedHeight(std::max(23, QFontMetrics(f).height() + 8));
    if (night_) { // the night header's background colors
        h->setStyleSheet(QString("QHeaderView::section { background-color: %1; color: %2; border: none;"
                                 " border-right: 1px solid %4; border-bottom: 1px solid %4; padding-left: 4px; }"
                                 "QHeaderView::section:hover { background-color: %3; }"
                                 "QHeaderView::section:pressed { background-color: %4; }")
                             .arg(LightMode::qc(nightMode_.headerBack).name(), LightMode::qc(lightMode_.headerFontColor).name(),
                                  LightMode::qc(nightMode_.headerHover).name(), LightMode::qc(nightMode_.headerDown).name()));
    } else {
        h->setStyleSheet(QString());
    }
}

// Customize Columns Header Font / Default Header Font (ELV_SetCustomHeaderFont)
void MainWindow::customizeHeaderFont(bool toDefault) {
    LightMode lm = loadLook();
    if (toDefault) {
        lm.defaultHeaderFont();
        if (night_) lm.headerFontColor = NightMode().look.headerFontColor; // clCream
    } else {
        bool ok = false;
        const QFont f = QFontDialog::getFont(&ok, lm.headerFont(), QApplication::activeWindow(), tr("Columns Header Font"));
        if (!ok) return;
        lm.setHeaderFont(f);
    }
    saveLook([&](LightMode& l) {
        l.headerFontName = lm.headerFontName;
        l.headerFontSize = lm.headerFontSize;
        l.headerFontStyle = lm.headerFontStyle;
        l.headerFontColor = lm.headerFontColor;
    });
    lightMode_.headerFontName = lm.headerFontName;
    lightMode_.headerFontSize = lm.headerFontSize;
    lightMode_.headerFontStyle = lm.headerFontStyle;
    lightMode_.headerFontColor = lm.headerFontColor;
    applyHeaderFont();
}

// Preferences page 2 (ini_files\lightmode.ini): PaintToolBarBk, SetColorsSearchPanel,
// SetColorsGamesListStatusBar, UpdateGamesListSplitterStyle, the selection bar and background.
void MainWindow::applyLightMode(const LightMode& lm) {
    lightMode_ = lm;
    applyHeaderFont();
    if (imageView_) { // image hint box
        ImageView::Hint h;
        h.color = LightMode::qc(lm.hintColor);
        h.frameColor = LightMode::qc(lm.hintFrameColor);
        h.text = LightMode::qc(lm.hintText);
        h.shadow = LightMode::qc(lm.hintShadow);
        h.frame = lm.hintFrame;
        h.textShadow = lm.hintShadowOn;
        h.italic = lm.hintItalic;
        h.opacity = lm.hintOpacity;
        h.position = lm.hintPosition;
        imageHint_ = h;
        forEachView([&h](ImageView* v) { v->setHint(h); });
    }
    if (layouts_) styleSplitters();
    auto panel = [](QWidget* w, const QString& style, const QColor& c1, const QColor& c2) {
        w->setProperty("vclStyle", style);
        w->setProperty("vclColor1", c1.name());
        w->setProperty("vclColor2", c2.name());
        w->update();
    };
    // Tool bars: the Windows theme (white rebar), or the gradient / solid colors.
    QColor top = LightMode::qc(lm.toolBarTop), bottom = LightMode::qc(lm.toolBarBottom);
    QString style = lm.toolBarWindowsTheme ? "vgSolid" : (lm.toolBarGradient ? "vgSimple" : "vgSolid");
    QColor c1 = lm.toolBarWindowsTheme ? QColor(Qt::white) : top, c2 = lm.toolBarWindowsTheme ? QColor(Qt::white) : bottom;
    for (QWidget* w : {static_cast<QWidget*>(ui_->PanelToolBar), static_cast<QWidget*>(ui_->PanelSearchGames_ToolBar),
                       static_cast<QWidget*>(ui_->PanelToolBarImages)})
        panel(w, style, c1, c2);
    // Search panel
    QPalette ep = ui_->FilterGameTitle_ToolBar->palette();
    ep.setColor(QPalette::Text, LightMode::qc(lm.searchTextFont));
    ep.setColor(QPalette::Base, LightMode::qc(lm.searchTextBack));
    ui_->FilterGameTitle_ToolBar->setPalette(ep);
    QPalette lp = ui_->LabelSearchGamesBy_ToolBar->palette();
    lp.setColor(QPalette::WindowText, LightMode::qc(lm.searchFieldFont));
    ui_->LabelSearchGamesBy_ToolBar->setPalette(lp);
    // Status bar
    panel(ui_->StatusBarPanel, lm.statusGradient ? "vgSimple" : "vgSolid", LightMode::qc(lm.statusTop), LightMode::qc(lm.statusBottom));
    for (QWidget* w : {static_cast<QWidget*>(ui_->StatusBar_GamesTotal), static_cast<QWidget*>(ui_->StatusBar_GamesGameName)}) {
        QPalette sp = w->palette();
        sp.setColor(QPalette::WindowText, LightMode::qc(lm.statusFont));
        w->setPalette(sp);
    }
    // Splitter between the games list and the images
    ui_->Splitter->setTmsStyle(lm.splitterStyle, LightMode::qc(lm.splitterSingle), LightMode::qc(lm.splitterSingleHot));
    ui_->Splitter->setGrip(lm.splitterGrip);
    // Image panel border ("Image 7 Pixels Border Color")
    ui_->PanelImage->setProperty("vclColor", LightMode::qc(lm.imageBorder).name());
    ui_->PanelImage->update();
    // Games list: selection bar, background color / image
    if (listDelegate_) listDelegate_->setLook(lm);
    if (iconView_) iconView_->setLook(lm);
    listBackground_ = QPixmap();
    if (lm.listImageEnabled) listBackground_.load(resolvePath(fe_.dataDir(), lm.listImage));
    QPalette pal = list_->palette();
    pal.setColor(QPalette::Base, LightMode::qc(lm.listBack));
    pal.setColor(QPalette::Highlight, Qt::transparent);  // the delegate draws the selection
    pal.setColor(QPalette::HighlightedText, LightMode::qc(lm.selFont));
    list_->setPalette(pal);
    list_->viewport()->setAutoFillBackground(listBackground_.isNull());
    list_->viewport()->update();
    if (docsReady_) applyDocsLook();
}


// Preferences that change the games list (TFormMain.MenuPreferencesClick applies them on close).
void MainWindow::applyPreferences() {
    WinIni& ini = fe_.ini();
    GamesModel::Options o;
    o.statusIcons = !ini.readBool("Preferences", "DisableStatusIcons", false);
    o.hideStatusTexts = ini.readBool("Preferences", "ShowHideStatusTexts", false);
    o.statusFirstLetter = ini.readBool("Preferences", "ShowStatusFirstLetterOnly", false);
    o.shortStatusTitles = ini.readBool("Preferences", "ShortDriverStatusColumnTitles", false);
    o.leadingZero = true; // (the Add Leading Zero setting is gone: versions always show as 0.xxx)
    o.grayscaleMissRoms = ini.readBool("Preferences", "ShowGrayscaleIconGamesMissROMsCHDs", false);
    o.hideSecondsPlayed = ini.readBool("Preferences", "HideSecondsLastPlayed", false);
    o.hideSecondsPlaytime = ini.readBool("Preferences", "HideSecondsTotalPlaytime", false);
    o.driverPath = ini.readBool("EmuLoaderRL", "ShowDriverPath", true);
    o.altTitleFormat = ini.readBool("Preferences", "NewTitleFormat", false);
    o.favoriteIcon = ini.readBool("Preferences", "ShowFavIconInGame", false);
    if (listDelegate_) listDelegate_->setCloneIndent(!ini.readBool("Preferences", "DisableCloneIndent", false));
    const bool multiline = ini.readBool("Preferences", "GameMultilineCaptions", false);
    if (listDelegate_) listDelegate_->setMultiline(multiline);
    if (machinesDelegate_) {
        machinesDelegate_->on = multiline;
        ui_->MachinesListSidePanel->viewport()->update();
    }
    // Protect Game Files (DisableDeleteSelectedGames): Delete, Copy and Move stay in the menu and say
    // why they do nothing (gameFiles); the original grayed out Delete only.
    o.playersIniOnly = ini.readBool("Preferences", "PlayersInfoFromIniOnly", false);
    o.titleExtra = std::clamp(ini.readInteger("Preferences", "TitleRemoveExtraText", 0), 0, 3);
    QByteArray header = list_->header()->saveState();
    QString keep = current() ? current()->key() : QString();
    model_->setStatusIconFolder(fe_.resourcesFolder() + "main_icons");
    model_->setOptions(o); // resets the model
    list_->header()->restoreState(header);
    filter_->setNaturalSorting(!ini.readBool("Preferences", "DisableNaturalSorting", false));
    selectedBeforeFilter_ = keep;
    applyLightMode(loadLook());
    applyConsoleComputer();
    if (docsReady_) { // Preferences: the documents' order and the display mode
        WinIni ex;
        ex.load(extrasIni());
        docsOrder_ = GameDocs::readOrder(ex);
        docsTabs_ = ex.readInteger("GameDocuments", "DisplayMode", 0) == 1;
        applyDocsLayout();
        updateDocs();
    }
}

// Preferences > Enable Console and Computer Support
void MainWindow::applyConsoleComputer() {
    bool on = PreferencesDialog::consoleComputerSupport(fe_);
    ElButton* b = ui_->ButtonGameFilterMachineTypeSystemsMulti;
    b->setProperty("elOverflow", false);
    b->setVisible(on && b->property("elShown").toBool());
    for (const char* m : {"PopupSetConsoleIniCustomOptions", "PopupDeleteConsoleIniCustomOptions",
                          "PopupSetComputerIniCustomOptions", "PopupDeleteComputerIniCustomOptions",
                          "PopupSetOtherSysIniCustomOptions", "PopupDeleteOtherSysIniCustomOptions"})
        if (QAction* a = menus_.action(m)) a->setVisible(on);
    layoutToolBar();
    if (list_ && filter_) {
        if (selectedBeforeFilter_.isEmpty()) rememberSelection();
        filter_->setConsoleComputer(on);
        afterFilterChange();
        showMachinesPanel();
    }
}

void MainWindow::setupList() {
    list_ = ui_->GamesListView;
    list_->setModel(filter_);
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setSortingEnabled(true);
    list_->setAllColumnsShowFocus(true);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->sortByColumn(GamesModel::Title, Qt::AscendingOrder);
    list_->header()->setSectionsMovable(true);
    list_->header()->setFirstSectionMovable(false);
    applyColumns();
    // Right-click the header to show/hide columns (PopupGamesColumns in the original).
    list_->header()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_->header(), &QWidget::customContextMenuRequested, this, [this](const QPoint& p) {
        QMenu m(this);
        // PopupGamesColumns (Customize Columns, the header font, Reset), then each column to show or hide
        if (QMenu* cols = menus_.menu("PopupGamesColumns")) {
            m.addActions(cols->actions());
            m.addSeparator();
        }
        for (int c = 1; c < GamesModel::ColumnCount; ++c) {
            QAction* a = m.addAction(GamesModel::columnTitle(c));
            a->setCheckable(true);
            a->setChecked(!list_->header()->isSectionHidden(c));
            connect(a, &QAction::toggled, this, [this, c](bool on) { list_->header()->setSectionHidden(c, !on); });
        }
        m.exec(list_->header()->mapToGlobal(p));
    });
    // The original's column header: Segoe UI 9, 23 pixels high; the last column keeps its own
    // width instead of stretching to the edge, so it can be resized like the others.
    list_->header()->setStretchLastSection(false);
    applyHeaderFont();
    list_->header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    setGameIconSize(fe_.ini().readInteger("Preferences", "GameIconSize", 3), false);
    list_->setFont(ScriptFonts::shown(model_->fonts().allSystems)); // GamesListView.Font: [All Systems] (else the parent font)
    list_->installEventFilter(this);
    listDelegate_ = new GamesListDelegate(list_, model_, filter_);
    listDelegate_->setFavoriteIcon(QIcon(fe_.mainIcon("favorite_game.ico")));
    list_->setItemDelegate(listDelegate_);
    list_->viewport()->installEventFilter(this);
    // With a background image the whole list is repainted on scroll, so the image stays put.
    for (QScrollBar* sb : {list_->verticalScrollBar(), list_->horizontalScrollBar()})
        connect(sb, &QScrollBar::valueChanged, this, [this] { if (!listBackground_.isNull()) list_->viewport()->update(); });
    connect(list_, &QTreeView::doubleClicked, this, [this] { runGame(); });
    connect(list_, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& p) { showGamesMenu(list_->viewport()->mapToGlobal(p)); });
    connect(list_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] {
        updateStatus();
        findVideoPreview(true); // (first: a Video panel shows "No Video" when there's none)
        updateImage();
        findManual();
        if (docsReady_) updateDocs();
    });
}

// The games list's menu (PopupMenuGamesList), for whichever view shows the games.
void MainWindow::showGamesMenu(const QPoint& global) {
    {
        QMenu* m = menus_.menu("PopupMenuGamesList");
        if (!m || !current()) return;
        // Show the real names in captions, as EmuLoader does ("Set [gamename] Custom Game Options").
        const MameGame* g = current();
        for (QAction* a : m->findChildren<QAction*>()) {
            if (!a->property("origText").isValid()) a->setProperty("origText", a->text());
            QString t = a->property("origText").toString();
            t.replace("[gamename]", "[" + g->name + "]").replace("[drivername]", "[" + QFileInfo(g->driver).completeBaseName() + "]");
            t.replace("[parentset]", "[" + (g->cloneOf.isEmpty() ? g->name : g->cloneOf) + "]");
            a->setText(t);
        }
        prepareCustomParamsMenu(*g);
        // A software list game: the machine it runs with; no custom settings of its own (the
        // machine's are used), as the original.
        const bool soft = g->isSoftware(), oneGame = list_->selectionModel()->selectedRows().size() <= 1;
        if (QAction* a = menus_.action("PopupMachineToRunGame")) a->setVisible(soft && oneGame && !filter_->machinePanel());
        if (QAction* a = menus_.action("PopupCustomSettings")) a->setVisible(!soft);
        // Add to / Remove from: only the one that applies for one game, both for several (PopupMenuGamesListPopup)
        const bool several = list_->selectionModel()->selectedRows().size() > 1;
        QList<QPair<QAction*, bool>> shown{
            {menus_.action("PopupAddToFavorites"), !filter_->favoritesOnly() && (several || !favorites_.contains(g->key()))},
            {menus_.action("PopupDeleteFromFavorites"), several || favorites_.contains(g->key())},
            {menus_.action("PopupAddToSpecialList"), several || !special_.contains(g->key())},
            {menus_.action("PopupRemoveFromSpecialList"), several || special_.contains(g->key())}};
        for (auto& [a, on] : shown)
            if (a) a->setVisible(on);
        m->exec(global);
        for (auto& [a, on] : shown)
            if (a) a->setVisible(true); // their shortcut keys work whatever the game
    }
}

// The views other than Details and Grouped: the same games and the same selection, drawn as
// icons, tiles or thumbnails, in the list's place.
void MainWindow::setupIconView() {
    iconView_ = new GamesIconView(model_, filter_, list_->parentWidget());
    iconView_->setSelectionModel(list_->selectionModel());
    iconView_->setContextMenuPolicy(Qt::CustomContextMenu);
    iconView_->setFont(list_->font());
    iconView_->hide();
    iconView_->installEventFilter(this);
    iconView_->setFavoriteIcon(QIcon(fe_.mainIcon("favorite_game.ico")));
    iconView_->setSpecialIcon(QIcon(fe_.mainIcon("special.ico")));
    iconView_->findSnapshot = [this](const QString& name, const QString& parent) {
        return imageFinder_.find(1, name, parent, 1); // the in-game snapshot (category 1)
    };
    connect(iconView_, &QListView::doubleClicked, this, [this] { runGame(); });
    connect(iconView_, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& p) { showGamesMenu(iconView_->viewport()->mapToGlobal(p)); });
    ThumbnailSettings ts;
    ts.load(fe_.gamesFiltersFile());
    iconView_->setThumbnails(ts);
    iconView_->setTileSize(fe_.ini().readInteger("Preferences", "TilesViewCellSize", 3));
    // View mode (ButtonViewModeDetailsClick): [Configuration] ViewMode
    const char* modes[] = {"ButtonViewModeDetails", "ButtonViewModeGrouped", "ButtonViewModeSmallIcons",
                           "ButtonViewModeLargeIcons", "ButtonViewModeTiles", "ButtonViewModeThumbnails"};
    for (int i = 0; i < 6; ++i) {
        if (QAction* a = menus_.action(modes[i])) a->setCheckable(true);
        menus_.on(modes[i], [this, i] { setViewMode(i); });
    }
    const char* tiles[] = {"PopupTilesViewCellSizeUltraLarge", "PopupTilesViewCellSizeMegaLarge",
                           "PopupTilesViewCellSizeExtraLarge", "PopupTilesViewCellSizeStandard"};
    for (int i = 0; i < 4; ++i) {
        if (QAction* a = menus_.action(tiles[i])) {
            a->setCheckable(true);
            a->setChecked(i == iconView_->tileSize());
        }
        menus_.on(tiles[i], [this, i, tiles] {
            iconView_->setTileSize(i);
            for (int k = 0; k < 4; ++k)
                if (QAction* a = menus_.action(tiles[k])) a->setChecked(k == i);
            fe_.ini().writeInteger("Preferences", "TilesViewCellSize", i);
            gamesView()->scrollTo(list_->currentIndex(), QAbstractItemView::PositionAtCenter);
        });
    }
    // Thumbnails: Customize, Show Parent Thumbnails, Show Images, Reload, Delete Selected Game Snapshot
    for (const char* n : {"MenuThumbnailSettings", "PopupThumbnailSettings"}) menus_.on(n, [this] { openSettings("Thumbnails"); });
    for (const char* n : {"MenuThumbnailShowParentThumbs", "PopupThumbnailShowParentThumbs"}) {
        if (QAction* a = menus_.action(n)) { a->setCheckable(true); a->setChecked(ts.showParent); }
        menus_.on(n, [this, n] {
            ThumbnailSettings t = iconView_->thumbnails();
            QAction* a = menus_.action(n);
            t.showParent = a && a->isChecked();
            for (const char* m : {"MenuThumbnailShowParentThumbs", "PopupThumbnailShowParentThumbs"})
                if (QAction* b = menus_.action(m)) b->setChecked(t.showParent);
            iconView_->setThumbnails(t);
            t.save(fe_.gamesFiltersFile());
        });
    }
    // Show Images: the image panel's Show Images (MenuShowImages), from here too
    for (const char* n : {"MenuThumbnailShowImages", "PopupThumbnailShowImages"}) {
        QAction* a = menus_.action(n);
        QAction* main = menus_.action("MenuShowImages");
        if (!a || !main) continue;
        a->setCheckable(true);
        a->setChecked(main->isChecked());
        connect(main, &QAction::toggled, a, &QAction::setChecked);
        menus_.on(n, [main, a] { if (main->isChecked() != a->isChecked()) main->trigger(); });
    }
    if (QAction* a = menus_.action("MenuThumbnailAlphaBlendedIcons")) a->setVisible(false); // drawn blended anyway
    menus_.on("PopupThumbnailReloadThumbnailsSelectedGames", [this] { iconView_->reloadThumbnails(true); });
    menus_.on("PopupThumbReloadAll", [this] { iconView_->reloadThumbnails(false); });
    menus_.on("PopupThumbDeleteSelectedGameSnapshot", [this] { // PopupThumbDeleteSelectedGameSnapshotClick
        const MameGame* g = current();
        if (!g || list_->selectionModel()->selectedRows().size() > 1) return;
        const ThumbnailSettings& t = iconView_->thumbnails();
        const ImageFinder::Found f = imageFinder_.find(1, g->imageName(), t.showParent ? g->imageParent() : QString(), 1);
        if (!f.ok || f.zipped()) return; // not in a zip file
        ImageDeleteRenameDialog::Input in;
        in.file = f.file;
        in.game = *g;
        in.gameIcon = model_->icon(*g);
        in.nameLine = ui_->StatusBar_GamesGameName->text();
        in.system = sys_ == SystemId::HBMAME ? QStringLiteral("HBMAME") : QStringLiteral("MAME");
        in.categoryIcon = toolIcon(QString(ImageFinder::category(1).icon) + ".ico", false);
        in.rename = false;
        if (ImageDeleteRenameDialog::run(in, this)) {
            iconView_->reloadThumbnails(true);
            updateImage();
        }
    });
    setViewMode(std::clamp(fe_.ini().readInteger("Configuration", "ViewMode", 0), 0, 5), false);
}

// Thumbnail Settings > Customize (MenuThumbnailSettingsClick)
void MainWindow::addThumbnailsPage(SettingsDialog& s, QVector<std::function<void()>>& after) {
    ThumbnailSettingsDialog::Input in;
    in.settings = iconView_->thumbnails();
    in.model = model_;
    in.filter = filter_;
    in.look = lightMode_;
    in.favoriteIcon = QIcon(fe_.mainIcon("favorite_game.ico"));
    in.specialIcon = QIcon(fe_.mainIcon("special.ico"));
    in.findSnapshot = iconView_->findSnapshot;
    in.apply = [this](const ThumbnailSettings& s) { iconView_->setThumbnails(s); };
    auto* dlg = new ThumbnailSettingsDialog(in, &s);
    s.addDialog(tr("Thumbnails"), dlg, "ButtonConfirm", "ButtonAbort"); // (the preview as big as the window allows)
    after << [this, dlg] {
        if (dlg->result() != QDialog::Accepted) return;
        iconView_->thumbnails().save(fe_.gamesFiltersFile());
        for (const char* n : {"MenuThumbnailShowParentThumbs", "PopupThumbnailShowParentThumbs"})
            if (QAction* a = menus_.action(n)) a->setChecked(iconView_->thumbnails().showParent);
    };
}

QAbstractItemView* MainWindow::gamesView() const {
    return iconView_ && iconView_->isVisibleTo(iconView_->parentWidget()) ? static_cast<QAbstractItemView*>(iconView_)
                                                                          : static_cast<QAbstractItemView*>(list_);
}

// ButtonViewModeDetailsClick / ViewModeToggle
void MainWindow::setViewMode(int mode, bool save) {
    const char* modes[] = {"ButtonViewModeDetails", "ButtonViewModeGrouped", "ButtonViewModeSmallIcons",
                           "ButtonViewModeLargeIcons", "ButtonViewModeTiles", "ButtonViewModeThumbnails"};
    for (int i = 0; i < 6; ++i)
        if (QAction* a = menus_.action(modes[i])) a->setChecked(i == mode);
    const bool hadFocus = gamesView()->hasFocus();
    QHeaderView* h = list_->header();
    if (mode == 1 && !filter_->grouped()) { // the columns can't sort the groups: keep the sort for later
        sortBeforeGroups_ = {h->sortIndicatorSection(), h->sortIndicatorOrder()};
        filter_->setGrouped(true);
        filter_->sort(GamesModel::Title, Qt::AscendingOrder);
        h->setSortIndicatorShown(false);
        h->setSectionsClickable(false);
        list_->setUniformRowHeights(false);
    } else if (mode != 1 && filter_->grouped()) {
        filter_->setGrouped(false);
        h->setSortIndicatorShown(true);
        h->setSectionsClickable(true);
        list_->setUniformRowHeights(true);
        list_->sortByColumn(sortBeforeGroups_.first, sortBeforeGroups_.second);
    }
    const bool icons = mode >= GamesIconView::SmallIcons;
    if (icons) {
        iconView_->setGeometry(list_->geometry());
        iconView_->setMode(mode);
    }
    iconView_->setVisible(icons);
    list_->setVisible(!icons);
    if (icons) iconView_->raise();
    for (const char* n : {"MenuCustomizeColumns", "MenuCustomizeColumnsHeaderFont", "MenuDefaultColumnsHeaderFont"})
        if (QAction* a = menus_.action(n)) a->setVisible(!icons); // IsDetailsView or IsGroupedView
    if (QAction* a = menus_.action("PopupThumbnails")) a->setVisible(mode == GamesIconView::Thumbnails);
    const QModelIndex cur = list_->currentIndex(); // after the sort changed
    if (cur.isValid()) gamesView()->scrollTo(cur, QAbstractItemView::PositionAtCenter);
    if (hadFocus) gamesView()->setFocus();
    if (save) fe_.ini().writeInteger("Configuration", "ViewMode", mode);
    viewMode_ = mode;
}

bool MainWindow::eventFilter(QObject* o, QEvent* e) {
    if (o == ui_->MachinesListSidePanel && e->type() == QEvent::KeyPress) { // Enter: the machine's games
        const int k = static_cast<QKeyEvent*>(e)->key();
        if (k == Qt::Key_Return || k == Qt::Key_Enter) {
            const QModelIndex i = ui_->MachinesListSidePanel->currentIndex();
            if (i.isValid()) useMachine(i.siblingAtColumn(0).data(Qt::UserRole + 1).toString());
            return true;
        }
    }
    if (dimensions_) {
        if (e->type() == QEvent::Resize && (qobject_cast<ImageView*>(o) || o == list_))
            QTimer::singleShot(0, this, [this] { updateLayoutDimensions(); });
    }
    if (iconView_ && o == list_ && (e->type() == QEvent::Resize || e->type() == QEvent::Move)) {
        iconView_->setGeometry(list_->geometry()); // the other views take the list's place
    }
    if (dimensions_) {
        if (e->type() == QEvent::MouseButtonRelease && o->objectName() == "PanelImageLayoutDimensButtonClose") {
            if (QAction* a = menus_.action("PopupImageShowLayoutDimensions")) a->trigger(); // unchecks it
            return true;
        }
    }
    if (o == ui_->ToolBarButtons && e->type() == QEvent::Resize && toolButtonHeight_) placeToolBarButtons();
    if (list_ && o == list_->viewport() && e->type() == QEvent::Paint && !listBackground_.isNull()) {
        // GamesListView.BackGround: the image tiled, or once at the top left, over the color.
        QPainter p(list_->viewport());
        p.fillRect(list_->viewport()->rect(), LightMode::qc(lightMode_.listBack));
        if (lightMode_.listTile) p.drawTiledPixmap(list_->viewport()->rect(), listBackground_);
        else p.drawPixmap(0, 0, listBackground_);
        return false; // the list draws its rows on top
    }
    if (list_ && o == list_ && (e->type() == QEvent::FocusIn || e->type() == QEvent::FocusOut))
        list_->viewport()->update(); // the selection bar has an "inactive" look
    if (iconView_ && o == iconView_ && (e->type() == QEvent::FocusIn || e->type() == QEvent::FocusOut))
        iconView_->viewport()->update();
    if ((o == list_ || (iconView_ && o == iconView_)) && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
            runGame();
            return true;
        }
        const QString t = k->text();
        if (!t.isEmpty() && t.at(0).isPrint() && !(k->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            incrementalSearch(t);
            return true;
        }
    }
    if (o == ui_->FilterGameTitle_ToolBar && e->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
        ui_->FilterGameTitle_ToolBar->clear(); // Esc clears the text (the list stays as it is)
        return true;
    }
    return QWidget::eventFilter(o, e);
}

// Typing in the games list selects the next game whose title (or set name: Menu Options > Games
// List > Incremental Search) starts with what was typed (GamesListViewIncrementalSearch).
void MainWindow::incrementalSearch(const QString& typed) {
    if (!incTimer_.isValid() || incTimer_.elapsed() > QApplication::keyboardInputInterval()) incBuffer_.clear();
    incTimer_.start();
    // the same letter again: the next game with it (as Windows lists do)
    const bool again = incBuffer_.size() == 1 && typed.compare(incBuffer_, Qt::CaseInsensitive) == 0;
    if (!again) incBuffer_ += typed;
    const bool byName = fe_.ini().readInteger("Preferences", "IncrementalSearch", 0) == 1;
    const int rows = filter_->rowCount();
    if (!rows) return;
    const int cur = list_->currentIndex().isValid() ? list_->currentIndex().row() : -1;
    const int first = incBuffer_.size() == 1 ? cur + 1 : std::max(cur, 0); // a longer text may still fit this one
    for (int n = 0; n < rows; ++n) {
        const int r = (first + n) % rows;
        const MameGame& g = model_->game(filter_->mapToSource(filter_->index(r, 0)).row());
        if ((byName ? g.name : model_->title(g)).startsWith(incBuffer_, Qt::CaseInsensitive)) {
            QModelIndex i = filter_->index(r, 0);
            list_->selectionModel()->setCurrentIndex(i, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            gamesView()->scrollTo(i);
            return;
        }
    }
}

// The games tool bar: shown or not, across the window or over the games list only (Bound to
// Games Panel: PanelToolBar's parent is PanelGamesArea, as in the original).
void MainWindow::applyToolBarPlacement() {
    QAction* show = menus_.action("MenuEnableToolBar");
    QAction* bound = menus_.action("MenuBoundToGamesPanel");
    const bool on = !show || show->isChecked(), inGames = bound && bound->isChecked();
    QWidget* bar = ui_->PanelToolBar;
    QWidget* old = bar->parentWidget();
    QWidget* want = inGames ? static_cast<QWidget*>(ui_->PanelGamesArea) : static_cast<QWidget*>(this);
    if (old != want) {
        const int h = bar->height();
        bar->setParent(want);
        bar->setGeometry(0, -1, want->width(), h); // first of the top-aligned panels
    }
    bar->setVisible(on);
    if (QAction* a = menus_.action("PopupShowToolBar")) a->setVisible(!on);
    if (QAction* a = menus_.action("PopupMainMenu")) a->setVisible(!on);
    WinIni& ini = fe_.ini();
    ini.writeInteger("ToolBar", "GamesFilters_Visible", on ? 1 : 0);
    ini.writeInteger("ToolBar", "GamesFilters_BoundToGamesPanel", inGames ? 1 : 0);
    vcl::relayout(this);
    vcl::relayout(ui_->PanelGamesArea);
    if (on) {
        vcl::relayout(bar);
        placeToolBarButtons();
    }
}

void MainWindow::setScreenshotsWidth(int w) {
    vcl::place(ui_->PanelScreenshotsArea, QRect(ui_->PanelScreenshotsArea->pos(), QSize(w, ui_->PanelScreenshotsArea->height())));
    vcl::relayout(this);
    updateImage();
}

const MameGame* MainWindow::current() const {
    QModelIndex i = list_->currentIndex();
    if (!i.isValid()) return nullptr;
    return &model_->game(filter_->mapToSource(i).row());
}

void MainWindow::loadGamesList() {
    QString err;
    filter_->setHbmame(sys_ == SystemId::HBMAME);
    imageFinder_.setSoftwareLists(SoftwareLists::made(softwareFolder())); // (the system's: their games' pictures)
    { // the multi-slot Neo-Geo MVS / ST-V games of this system (Misc Filters, made with the games list)
        const QString base = Frontend::fileBase(sys_);
        multiSlot_ = MiscFilters::loadMultiSlot(MultiSlot::cartFile(fe_.gamesFolder(), base, "neogeo"),
                                                MultiSlot::cartFile(fe_.gamesFolder(), base, "stv"));
        filter_->setMiscFilters(filter_->miscFilters(), multiSlot_);
    }
    // Create MAME / HBMAME Software Games List: the system shown
    if (QAction* a = menus_.action("MenuCreateMAMESoftwareListGames")) a->setVisible(sys_ == SystemId::MAME);
    if (QAction* a = menus_.action("MenuCreateHBMAMESoftwareListGames")) a->setVisible(sys_ == SystemId::HBMAME);
    if (QFileInfo::exists(fe_.databaseFile(sys_))) {
        StartupSplash::status(tr("Load Games List"), tr("Loading games list."));
        // read on another thread while the splash screen's effect plays (at startup)
        ElDatabase fresh;
        StartupSplash::runWhileShowing([&] {
            fresh.load(fe_.databaseFile(sys_), fe_.statusFile(sys_), &err);
            fresh.loadCatver(QDir(fe_.dataDir()).filePath("arcade/filters/catver.ini")); // Category / Version Added
            fresh.loadSoftwareLists(fe_.softwareListsFile(sys_));                      // machines with software lists
            fresh.loadMessIni(QDir(fe_.dataDir()).filePath("arcade/filters/mess.ini"));  // other non-arcade machines
            fresh.loadNPlayers(QDir(fe_.dataDir()).filePath("arcade/filters/nplayers.ini")); // Number of Players
            fresh.loadLanguages(QDir(fe_.dataDir()).filePath("arcade/filters/languages.ini")); // Language
            // the software list games (arcade\mame_softwarelist_games), after the machines
            fresh.setSoftware(SoftwareLists::loadAll(softwareFolder(), SoftwareLists::excluded(softwareExcludeFile())));
        });
        db_ = std::move(fresh);
        loadPlayStats();
        model_->reload();
        applyColumns(); // a model reset clears the header layout
        // At startup, the game of Select Game at Startup when there's one (ReselectItem), else the
        // last selected game.
        QStringList names{fe_.ini().readString("Selected", "LastSelected")};
        if (!startedUp_ && fe_.ini().readInteger(kStartupSection, "SystemID", -1) == int(sys_))
            names.prepend(startupGame());
        startedUp_ = true;
        for (const QString& name : names) {
            QModelIndex sel;
            for (int r = 0; r < filter_->rowCount() && !sel.isValid(); ++r)
                if (model_->game(filter_->mapToSource(filter_->index(r, 0)).row()).key() == name) sel = filter_->index(r, 0);
            if (!sel.isValid()) continue;
            list_->setCurrentIndex(sel);
            gamesView()->scrollTo(sel, QAbstractItemView::PositionAtCenter);
            break;
        }
        if (!list_->currentIndex().isValid() && filter_->rowCount()) list_->setCurrentIndex(filter_->index(0, 0));
        gamesView()->setFocus(); // FocusGamesList: the selection bar starts in its active colors
    }
    if (filter_->machineTypes().machinesPanel) showMachinesPanel(); // its machines from the new list
    fillCpuList();
    showCpuFilter(filter_->miscFilters().mainCpu);
    if (!sevenZipAsked_) { // a slow .7z picture archive: offered once a session, when the window is up
        sevenZipAsked_ = true;
        QTimer::singleShot(400, this, [this] { offerSevenZipConversion(true); });
    }
    updateStatus();
    DiagnosticLog::write("games", QString("LoadGames completed; system=%1; items=%2 (software list games %3); shown=%4; file=%5")
                                      .arg(Frontend::section(sys_)).arg(db_.games().size()).arg(db_.softwareCount())
                                      .arg(filter_->rowCount()).arg(QDir::toNativeSeparators(fe_.databaseFile(sys_))));
}

void MainWindow::updateStatus() {
    // StatusBar_GamesTotal: "000000 Games"
    // "11606 Games" - the label sizes itself and the game name follows it.
    ui_->StatusBar_GamesTotal->setText(tr("%1 Games").arg(filter_->rowCount()));
    updateEmptyListMessage(); // (why there are none, when there are none)
    ui_->StatusBar_GamesTotal->adjustSize();
    QRect t = ui_->StatusBar_GamesTotal->geometry();
    vcl::place(ui_->StatusBar_GamesTotal, QRect(t.x(), t.y(), t.width(), t.height()));
    QRect n = ui_->StatusBar_GamesGameName->geometry();
    vcl::place(ui_->StatusBar_GamesGameName, QRect(t.right() + 12, n.y(), std::max(100, ui_->StatusBarPanel->width() - t.right() - 16), n.height()));
    const MameGame* g = current();
    if (!g) {
        ui_->StatusBar_GamesGameName->setText(db_.games().isEmpty()
                                                  ? tr("No games list yet")
                                                  : QString());
        return;
    }
    QString s = g->name;
    if (!g->cloneOf.isEmpty()) s += " [" + g->cloneOf + "]";
    if (!g->bios.isEmpty()) s += " [" + g->bios + "]";
    if (g->merged) s += " [merged]";
    if (g->isSoftware()) s += " [software " + g->softwareList + "]";
    ui_->StatusBar_GamesGameName->setText(s);
}

// ------------------------------------------------------------ image panel
// Port of TFormMain.LoadScreenshot, ButtonViewImage, CycleImageCategory, CallSelectImageCategory,
// the image layouts (SetImageLayout, CycleImageLayout, CallSelectImageLayout) and the image menu.

ImageView* MainWindow::newImageView(QWidget* parent) {
    auto* v = new ImageView(parent);
    v->setScale(imageScale_);
    v->setHint(imageHint_);
    connect(v, &ImageView::contextMenuWanted, this, [this, v](const QPoint& at) {
        popupView_ = v;
        // Rename / Delete Image: a picture of the game's, not in a zip file
        bool file = !v->property("imageFile").toString().isEmpty();
        for (const char* item : {"PopupRenameCurrentImage", "PopupDeleteCurrentImage"})
            if (QAction* a = menus_.action(item)) a->setEnabled(file);
        if (QMenu* m = menus_.menu("PopupMenuImages")) m->exec(at);
    });
    v->installEventFilter(this); // Show Layout Dimensions follows the pictures' sizes
    connect(v, &ImageView::wheelStep, this, [this, v](int step, bool ctrl) {
        // Ctrl + wheel: this image's next / previous picture. The wheel alone: this image's next /
        // previous category (the layout stays: the < > buttons, F9 and Ctrl+F9 change it).
        if (ctrl) stepImage(step, slotOf(v));
        else cycleSlotCategory(slotOf(v), step > 0);
    });
    return v;
}

int MainWindow::slotOf(const ImageView* v) const {
    for (int i = 1; i <= 4; ++i)
        if (layouts_->view(i) == v) return i;
    return 0;
}

int MainWindow::slotCategory(int slot) const {
    if (layouts_->layout() == 0) return imageCategory_;
    // SetImageCategoryToDisplay: console/computer machines can have their own categories.
    const MameGame* g = current();
    bool cc = useConsComp_ && g && g->systemType == 1;
    return std::clamp((cc ? layoutSettings_.consComp : layoutSettings_.category)[slot - 1], 0, ImageFinder::Count - 1);
}

void MainWindow::forEachView(const std::function<void(ImageView*)>& f) {
    for (int i = 1; i <= 4; ++i)
        if (ImageView* v = layouts_ ? layouts_->view(i) : nullptr) f(v);
}

void MainWindow::setupImagePanel() {
    ui_->Images->hide();
    WinIni& ini = fe_.ini();
    imageScale_.stretch = ini.readBool("Images", "Stretch", true);
    imageScale_.largerOnly = ini.readBool("Images", "StretchLargerImages", false);
    imageScale_.ignoreAspect = ini.readBool("Images", "DisableAspectRatio", false);
    displayParent_ = ini.readBool("Images", "DisplayParent", true);
    snapWhenNoArtwork_ = ini.readBool("Images", "DisplayGameSnapWhenNoArtwork", false);
    showImages_ = ini.readBool("Images", "ShowImages", true);
    imageView_ = newImageView(ui_->PanelImage);
    layouts_ = std::make_unique<ImageLayouts>(ui_->PanelImage, imageView_);
    // ini_files\image_category.ini: background color and "enabled" per category, single color
    WinIni cat;
    cat.load(fe_.iniFilesFolder() + "image_category.ini");
    for (int i = 0; i < ImageFinder::Count; ++i) {
        const char* sec = ImageFinder::category(i).section;
        categoryBack_[i] = cat.readInteger("Category", QString(sec) + "_bkcolor", 0);
        categoryEnabled_[i] = i == 1 || cat.readBool("Category", QString(sec) + "_enabled", true);
    }
    videoCategoryOn_ = cat.readBool("EmuLoaderRL", "Video_enabled", true);
    singleBack_ = cat.readBool("SingleBackground", "Enabled", false);
    singleBackColor_ = cat.readInteger("SingleBackground", "Color", 0);
    // ini_files\screenshot_layouts.ini [Single] image1_category (ReadSingleLayoutCategory)
    WinIni lay;
    lay.load(fe_.iniFilesFolder() + "screenshot_layouts.ini");
    imageCategory_ = std::clamp(lay.readInteger("Single", "image1_category", 1), 0, ImageFinder::Count - 1);
    imageVideo_ = lay.readBool("Single", "image1_video", false);
    if (imageVideo_) imageCategory_ = 1;
    setupImageFinder();

    // Image tool bar buttons
    auto on = [this](QPushButton* b, const QString& tip, std::function<void()> f) {
        b->setEnabled(true);
        b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, std::move(f));
    };
    on(ui_->ButtonPreviousImage, tr("Previous image of this game (F11, or Ctrl + mouse wheel up over an image)"), [this] { stepImage(-1); });
    on(ui_->ButtonNextImage, tr("Next image of this game (F12, or Ctrl + mouse wheel down over an image)"), [this] { stepImage(1); });
    on(ui_->ButtonPreviousCategory, tr("Previous image category (Ctrl+F9, or mouse wheel up over the image)"), [this] { cycleCategory(false); });
    on(ui_->ButtonNextCategory, tr("Next image category (F9, or mouse wheel down over the image)"), [this] { cycleCategory(true); });
    on(ui_->ButtonImageCategory, tr("Select the image category to show"), [this] { selectImageCategory(); });
    on(ui_->ButtonPreviousLayout, tr("Previous layout (in a multi-image layout: Ctrl+F9)"), [this] { cycleLayout(false); });
    on(ui_->ButtonNextLayout, tr("Next layout (in a multi-image layout: F9)"), [this] { cycleLayout(true); });
    on(ui_->ButtonScreenshotLayouts, tr("Select the image layout"), [this] { selectImageLayout(); });
    on(ui_->ButtonImagePanelToggle, tr("Show or hide images (F6)"), [this] {
        showImages_ = !showImages_;
        updateImage();
    });
    // Keyboard (KeyboardMouse browse mode): F6 images on/off, F9 / Ctrl+F9 category (layout),
    // F11 / F12 previous / next image.
    auto key = [this](QKeySequence k, std::function<void()> f) {
        auto* sc = new QShortcut(k, this);
        connect(sc, &QShortcut::activated, this, std::move(f));
    };
    key(Qt::Key_F6, [this] { if (QAction* a = menus_.action("MenuShowImages")) a->trigger(); });
    key(Qt::Key_F9, [this] { if (layouts_->layout() == 0) cycleCategory(true); else cycleLayout(true); });
    key(QKeySequence(Qt::CTRL | Qt::Key_F9), [this] { if (layouts_->layout() == 0) cycleCategory(false); else cycleLayout(false); });
    key(Qt::Key_F11, [this] { stepImage(-1); });
    key(Qt::Key_F12, [this] { stepImage(1); });

    // Image popup menu (PopupMenuImages)
    auto check = [this](const char* item, bool value, std::function<void(bool)> f) {
        QAction* a = menus_.action(item);
        if (!a) return;
        a->setCheckable(true);
        a->setChecked(value);
        menus_.on(item, [a, f] { f(a->isChecked()); });
    };
    auto rescale = [this] { forEachView([this](ImageView* v) { v->setScale(imageScale_); }); };
    check("PopupImageStretch", imageScale_.stretch, [this, rescale](bool v) { imageScale_.stretch = v; rescale(); });
    check("PopupImageStretchLarger", imageScale_.largerOnly, [this, rescale](bool v) { imageScale_.largerOnly = v; rescale(); });
    check("PopupImagesDisableAspectRatio", imageScale_.ignoreAspect, [this, rescale](bool v) { imageScale_.ignoreAspect = v; rescale(); });
    check("PopupImageDisplayParent", displayParent_, [this](bool v) { displayParent_ = v; updateImage(); });
    check("PopupImageUseSingleBackgroundColor", singleBack_, [this](bool v) {
        if (v) {
            QColor c = QColorDialog::getColor(ElColorBox::toQColor(singleBackColor_), this, tr("Single Background Color"));
            if (c.isValid()) singleBackColor_ = ElColorBox::fromQColor(c);
        }
        singleBack_ = v;
        updateImage();
    });
    menus_.on("PopupShowFirstImage", [this] { slotIndex_.fill(1); updateImage(); });
    menus_.on("PopupRenameCurrentImage", [this] { deleteRenameImage(true); });
    menus_.on("PopupDeleteCurrentImage", [this] { deleteRenameImage(false); });
    menus_.on("MenuImagesDeleteCloneGameImages", [this] { deleteCloneImages(); });
    // Menu Options > Images > Resampler: how pictures are scaled ([Images] ImageResampler, Cosine by default)
    {
        const int now = std::clamp(ini.readInteger("Images", "ImageResampler", 2), 0, 3);
        ImageView::setResampler(Resampler(now));
        const QPair<const char*, int> items[] = {{"MenuImageFilterOff", 0}, {"MenuImageFilterLinear", 1},
                                                 {"MenuImageFilterCosine", 2}, {"MenuImageFilterHermite", 3}};
        auto* group = new QActionGroup(this);
        for (const auto& item : items) {
            const char* n = item.first;
            const int v = item.second;
            QAction* a = menus_.action(n);
            if (!a) continue;
            a->setCheckable(true);
            a->setChecked(v == now);
            group->addAction(a);
            menus_.on(n, [this, v] {
                ImageView::setResampler(Resampler(v));
                fe_.ini().writeInteger("Images", "ImageResampler", v);
                forEachView([](ImageView* view) { view->update(); });
            });
        }
    }
    menus_.on("PopupImageCategorySettings", [this] { openSettings("Pictures"); });
    menus_.on("MenuImageCategorySettings", [this] { openSettings("Pictures"); });
    menus_.on("MenuImagesManager", [this] { openImagesManager(); });
    // Video previews: Shift+V is the menu item's shortcut
    menus_.on("PopupVideoPreviewPlay", [this] { toggleVideoPreview(); });
    for (const char* n : {"PopupVideoPreviewSettings", "PopupImageVideoPreviewSettings", "MenuImageVideoPreviewSettings"})
        menus_.on(n, [this] { openSettings("Video"); });
    connect(ui_->ButtonVideoPreviewPlay, &QPushButton::clicked, this,
            [this] { if (QAction* a = menus_.action("PopupVideoPreviewPlay")) a->trigger(); });
    loadVideoPreview();
    // View Game Manual (PDF): the games list menu and the images tool bar (OpenGameManualPDF)
    menus_.on("PopupViewGameManualPDF", [this] { openManual(); });
    // Internet Game Info: the game's page in the browser (local, or the website in Preferences)
    menus_.on("PopupInternetGameInfo", [this] { openInternetGameInfo(); });
    ui_->ButtonInternetGameInfo->setCheckable(false); // the original's panel: here a page in the browser
    ui_->ButtonInternetGameInfo->setEnabled(true);
    ui_->ButtonInternetGameInfo->setToolTip(tr("Show the selected game's page in your web browser: made from your files, or a website (Settings > Game Info and Documents)"));
    connect(ui_->ButtonInternetGameInfo, &QPushButton::clicked, this, [this] { openInternetGameInfo(); });
    connect(ui_->ButtonOpenManualPDF, &QPushButton::clicked, this, [this] { openManual(); });
    findManual();
    check("PopupImageShowLayoutDimensions", false, [this](bool v) { showLayoutDimensions(v); });
    check("PopupImageShowToolBar", ini.readBool("ToolBar", "ImagesBar_Visible", true), [this](bool v) {
        ui_->PanelToolBarImages->setVisible(v);
        vcl::relayout(ui_->PanelToolBarImages->parentWidget());
    });
    ui_->PanelToolBarImages->setVisible(ini.readBool("ToolBar", "ImagesBar_Visible", true));
    // Layout splitters
    menus_.on("PopupImageCenterAllSplitters", [this] { layouts_->centerSplitters(); styleSplitters(); updateImage(); });
    check("PopupImageShowSplitterGrip", lightMode_.imgSplitterGrip, [this](bool v) {
        lightMode_.imgSplitterGrip = v;
        saveLook([v](LightMode& l) { l.imgSplitterGrip = v; });
        styleSplitters();
    });
    menus_.on("PopupImageLayoutSettings", [this] { openSettings("Layouts"); });
    menus_.on("PopupImageCustomizeSplitters", [this] { openSettings({}, "ImageSplittersBox"); });
    // The splitter bar's own menu: the same as the image menu's, plus Center This Splitter
    // (after the menu is gone: the layout is made again, and the splitter with it)
    menus_.on("CenterSplitter1", [this] {
        QTimer::singleShot(0, this, [this] {
            if (splitterForMenu_) layouts_->centerSplitter(splitterForMenu_);
            styleSplitters();
            updateImage();
        });
    });
    menus_.on("MenuItem11", [this] {
        QTimer::singleShot(0, this, [this] { if (QAction* a = menus_.action("PopupImageCenterAllSplitters")) a->trigger(); });
    });
    menus_.on("MenuItem13", [this] { if (QAction* a = menus_.action("PopupImageCustomizeSplitters")) a->trigger(); });
    if (QAction* a = menus_.action("PopupShowSplitterGrip")) a->setCheckable(true);
    menus_.on("PopupShowSplitterGrip", [this] { if (QAction* a = menus_.action("PopupImageShowSplitterGrip")) a->trigger(); });
    // Main menu > Images: the same settings as the image menu
    auto mirror = [this](const char* item, const char* popup) {
        QAction* a = menus_.action(item);
        QAction* b = menus_.action(popup);
        if (!a || !b) return;
        a->setCheckable(true);
        a->setChecked(b->isChecked());
        menus_.on(item, [a, b] { if (b->isChecked() != a->isChecked()) b->trigger(); });
        connect(b, &QAction::toggled, a, &QAction::setChecked);
    };
    mirror("MenuImagesEnableToolBar", "PopupImageShowToolBar");
    mirror("MenuImageUseSingleBackgroundColor", "PopupImageUseSingleBackgroundColor");
    menus_.on("MenuImageLayoutSettings", [this] { openSettings("Layouts"); });
    menus_.on("MenuImageConvertSevenZip", [this] { offerSevenZipConversion(false); });
    check("MenuShowImages", showImages_, [this](bool v) {
        showImages_ = v;
        if (docsReady_) applyDocsLayout(); // with the game docs on, they fill the panel
        updateImage();
    });
    check("PopupDisplayGameSnapshotifGameArtworkNotFound", snapWhenNoArtwork_, [this](bool v) {
        snapWhenNoArtwork_ = v;
        updateImage();
    });
    updateCategoryButton();
}

void MainWindow::refreshImageFolders() {
    setupImageFinder();
    updateImage();
    if (docsOn_) { // ui.ini's historypath may have changed too
        reloadDocs();
        updateDocs();
    }
}

void MainWindow::updateCategoryButton() {
    const bool video = imageVideo_ && videoInPanels();
    QIcon ic = toolIcon(video ? QStringLiteral("video_preview.ico") : QString(ImageFinder::category(imageCategory_).icon) + ".ico", false);
    for (const QSize& sz : ic.availableSizes()) ic.addPixmap(ic.pixmap(sz), QIcon::Disabled);
    ui_->ButtonImageCategory->setIcon(ic);
    ui_->ButtonImageCategory->setToolTip(
        tr("Image category: %1\nClick to select another").arg(video ? tr("Video") : ImageFinder::title(imageCategory_)));
}

// c: a picture category, or ImageLayouts::Video (the snapshot, with the video over it)
void MainWindow::setImageCategory(int c) {
    const bool video = c == ImageLayouts::Video;
    if (video) c = 1;
    if (c == imageCategory_ && video == imageVideo_) return;
    stopVideo(); // another category: its picture, not the video
    if (c != imageCategory_) slotIndex_[0] = 1;
    imageCategory_ = c;
    imageVideo_ = video;
    updateCategoryButton();
    updateImage();
    videoPanelsChanged();
}

// CycleImageCategory: the next category that's enabled in Category Settings; Video after the
// Game Snapshot, while video previews play in the panels
int MainWindow::nextCategory(int now, bool video, bool forward) const {
    QList<int> order;
    for (int c = 0; c < ImageFinder::Count; ++c) {
        if (categoryEnabled_[c]) order << c;
        if (c == 1 && videoInPanels() && videoCategoryOn_) order << ImageLayouts::Video;
    }
    if (order.isEmpty()) return now;
    if (video && videoInPanels()) now = ImageLayouts::Video;
    const int at = std::max<int>(0, int(order.indexOf(now)));
    return order[(at + (forward ? 1 : int(order.size()) - 1)) % order.size()];
}

void MainWindow::cycleCategory(bool forward) {
    if (layouts_->layout() != 0) return; // categories are set per layout in Layout Settings
    setImageCategory(nextCategory(imageCategory_, imageVideo_, forward));
}

// The mouse wheel over a picture: that picture's next / previous category. In a multi-image layout
// the panel's category is the layout's (Settings > Layouts), saved there.
void MainWindow::cycleSlotCategory(int slot, bool forward) {
    if (layouts_->layout() == 0 || slot < 1 || slot > 4) {
        cycleCategory(forward);
        return;
    }
    const MameGame* g = current();
    const bool cc = useConsComp_ && g && g->systemType == 1;
    auto& cats = cc ? layoutSettings_.consComp : layoutSettings_.category;
    auto& videos = cc ? layoutSettings_.videoCC : layoutSettings_.video;
    const int next = nextCategory(cats[slot - 1], videos[slot - 1], forward);
    const bool video = next == ImageLayouts::Video;
    if ((video ? 1 : next) == cats[slot - 1] && video == videos[slot - 1]) return;
    stopVideo();
    cats[slot - 1] = video ? 1 : next;
    videos[slot - 1] = video;
    slotIndex_[slot - 1] = 1;
    const QString file = fe_.iniFilesFolder() + "screenshot_layouts.ini";
    WinIni lay;
    lay.load(file);
    ImageLayouts::writeCategories(lay, layouts_->layout(), layoutSettings_);
    lay.save(file);
    updateImage();
    videoPanelsChanged();
}

// FormImageCategorySelector: under the category button (tool bar), or centered with every
// category listed (Layout Settings). Returns the chosen category, or -1.
int MainWindow::pickCategory(int current, QWidget* under) {
    QDialog dlg(this, under ? Qt::Tool : Qt::Dialog);
    Ui::FormImageCategorySelector ui;
    ui.setupUi(&dlg);
    vcl::apply(&dlg);
    dlg.setWindowTitle(tr("Image Category"));
    auto* model = new QStandardItemModel(&dlg);
    for (int i = 0; i < ImageFinder::Count; ++i) {
        if (under && !categoryEnabled_[i]) continue;
        auto* it = new QStandardItem(toolIcon(QString(ImageFinder::category(i).icon) + ".ico", false), ImageFinder::title(i));
        it->setData(i);
        model->appendRow(it);
        if (i == 1 && ((videoInPanels() && (videoCategoryOn_ || !under)) || current == ImageLayouts::Video)) { // Video, after the Game Snapshot
            auto* v = new QStandardItem(toolIcon("video_preview.ico", false), tr("Video"));
            v->setData(ImageLayouts::Video);
            model->appendRow(v);
        }
    }
    ui.CategoriesListView->setModel(model);
    ui.CategoriesListView->setRootIsDecorated(false);
    ui.CategoriesListView->setHeaderHidden(true);
    ui.CategoriesListView->setIconSize(QSize(32, 32));
    ui.CategoriesListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int r = 0; r < model->rowCount(); ++r)
        if (model->item(r)->data().toInt() == current) ui.CategoriesListView->setCurrentIndex(model->index(r, 0));
    connect(ui.ButtonOk, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(ui.ButtonCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(ui.CategoriesListView, &QTreeView::doubleClicked, &dlg, &QDialog::accept);
    ui.LabelSystemTitle->adjustSize();
    if (under) dlg.move(popupPos(under, dlg.size()));
    if (dlg.exec() != QDialog::Accepted) return -1;
    QModelIndex i = ui.CategoriesListView->currentIndex();
    return i.isValid() ? model->itemFromIndex(i)->data().toInt() : -1;
}

void MainWindow::selectImageCategory() {
    int c = pickCategory(imageVideo_ && videoInPanels() ? ImageLayouts::Video : imageCategory_, ui_->ButtonImageCategory);
    if (c >= 0) setImageCategory(c);
}

// Under a tool bar button, kept on screen.
QPoint MainWindow::popupPos(QWidget* button, const QSize& size) const {
    QPoint at = button->mapToGlobal(QPoint(10, button->height() + 1));
    if (QScreen* scr = button->screen()) {
        QRect r = scr->availableGeometry();
        at.setX(std::clamp(at.x(), r.left(), std::max(r.left(), r.right() - size.width())));
        at.setY(std::clamp(at.y(), r.top(), std::max(r.top(), r.bottom() - size.height())));
    }
    return at;
}

void MainWindow::stepImage(int step, int onlySlot) {
    // ButtonViewImage: each image moves to its next / previous picture, if there is one.
    const MameGame* g = current();
    if (!g) return;
    QString parent = displayParent_ ? g->imageParent() : QString();
    bool changed = false;
    for (int i = 1; i <= 4; ++i) {
        if (!layouts_->view(i) || (onlySlot && i != onlySlot)) continue;
        int n = slotIndex_[i - 1] + step;
        if (n < 1 || n > ImageFinder::MaxImages) continue;
        if (!imageFinder_.find(slotCategory(i), g->imageName(), parent, n).ok) continue;
        slotIndex_[i - 1] = n;
        changed = true;
    }
    if (changed) updateImage();
}

void MainWindow::updateImage() {
    if (!layouts_) return;
    const MameGame* g = current();
    if (g && g->key() != imageGame_) { // a new game starts at its first images
        imageGame_ = g->key();
        slotIndex_.fill(1);
    }
    for (int i = 1; i <= 4; ++i)
        if (ImageView* v = layouts_->view(i)) {
            // a Video panel, for a game without a video: says so, as the pictures do
            if (g && slotVideo(i) && videoGame_ == g->key()) {
                if (videoFile_.isEmpty()) showNoVideo(v);
                else showVideoWaiting(v); // (the video goes over it)
            } else {
                loadImage(v, slotCategory(i), slotIndex_[i - 1], g);
            }
        }
}

// Why a game's picture isn't there, in a sentence; fix: a click on it should open Settings > Pictures
QString MainWindow::notFoundReason(int category, const ImageFinder::Found& f, bool* fix) const {
    const QString title = ImageFinder::title(category);
    if (f.ok) { // found, but it couldn't be read
        const QString name = f.zipped() ? QFileInfo(f.file).fileName() + QDir::separator() + f.entry : QDir::toNativeSeparators(f.file);
        return tr("%1 couldn't be read:").arg(name) + '\n' + tr("the file may be damaged.");
    }
    const QStringList folders = imageFinder_.folders(category);
    QStringList places;
    for (const QString& d : folders)
        if (QFileInfo(d).isDir()) places << tr("the %1 folder").arg(QDir(d).dirName());
    for (const QString& z : imageFinder_.zipFiles(category))
        if (QFileInfo(z).isFile()) places << QFileInfo(z).fileName();
    if (places.isEmpty()) {
        *fix = true;
        if (folders.isEmpty()) return tr("No folder is set for %1.").arg(title) + '\n' + tr("Click here to set one in Settings > Pictures.");
        return tr("The %1 folder isn't there:").arg(title) + '\n' + QDir::toNativeSeparators(folders.first()) + '\n' +
               tr("Click here to set it in Settings > Pictures.");
    }
    if (places.size() > 3) places = places.mid(0, 3);
    const QString where = places.size() == 1 ? places[0]
                          : places.size() == 2 ? tr("%1 or %2").arg(places[0], places[1])
                                               : tr("%1, %2 or %3").arg(places[0], places[1], places[2]);
    return tr("Not in %1.").arg(where);
}

void MainWindow::loadImage(ImageView* v, int category, int& index, const MameGame* g) {
    int bg = singleBack_ ? singleBackColor_ : categoryBack_[category];
    v->setBackground(ElColorBox::toQColor(bg));
    const ImageCategory& cat = ImageFinder::category(category);
    QIcon icon = toolIcon(QString(cat.icon) + ".ico", false);
    if (!showImages_) {
        v->setImage({}, false);
        v->setHintText({}, {}, {}, false);
        return;
    }
    ImageFinder::Found f;
    v->setProperty("imageFile", QString());
    v->setProperty("imageZip", QString()); // (zip|entry)
    v->setProperty("imageCategory", category);
    v->setProperty("noVideo", false);
    v->setProperty("videoWaiting", false);
    if (g) {
        QString parent = displayParent_ ? g->imageParent() : QString();
        f = imageFinder_.find(category, g->imageName(), parent, index);
        if (!f.ok && index > 1) { index = 1; f = imageFinder_.find(category, g->imageName(), parent, 1); }
        // Game Artwork: the in-game snapshot when there's none (DisplayGameSnapWhenNoArtwork)
        if (!f.ok && category == 8 && snapWhenNoArtwork_) f = imageFinder_.find(1, g->imageName(), parent, 1);
    }
    // The image is read and decoded in the background (big flyers, zips with thousands of images,
    // network drives); only the last one asked for is shown in the panel. (The original's threads
    // could show the wrong one, hence its Disable Threaded Preview Images Loading.)
    const int gen = v->property("imageGen").toInt() + 1;
    v->setProperty("imageGen", gen);
    // the panel's picture wanted now: an older one still being read (passing games quickly) is
    // skipped, or stopped part way through a .7z's big block, rather than read for nothing first
    std::shared_ptr<std::atomic<int>>& wanted = imageWanted_[v];
    if (!wanted) wanted = std::make_shared<std::atomic<int>>(0);
    wanted->store(gen);
    const bool hasGame = g != nullptr;
    auto show = [this, v, category, icon, index, f, hasGame](const QImage& loaded) {
        QImage img = loaded;
        if (img.isNull()) {
            // why there's none: no folder set, the folder gone, not in it, or the file unreadable
            bool fix = false;
            const QString reason = hasGame ? notFoundReason(category, f, &fix) : QString();
            // the user's own picture (resources\images\img_missing\<category>.png, as the original's
            // LoadNotAvailableImg) as it is; otherwise the category's picture with the reason, drawn
            // in the user's language
            QString missing = fe_.resourcesFolder() + "images/img_missing/" + ImageFinder::category(category).missing;
            if (img.load(missing)) {
                v->setImage(img, true);
            } else {
                const ImageCategory& c = ImageFinder::category(category);
                const QIcon picture = ToolBarIcons::make(fe_.mainIcon(QString(c.icon) + ".ico"), {}, QSize(220, 188), true);
                v->setNotFound(picture, tr("No %1").arg(ImageFinder::title(category)), reason, fix);
                if (!v->property("notFoundConnected").toBool()) {
                    v->setProperty("notFoundConnected", true);
                    connect(v, &ImageView::notFoundClicked, this,
                            [this, v] { openSettings(v->property("noVideo").toBool() ? "Video" : "Pictures"); });
                }
            }
            v->setHintText(icon, ImageFinder::title(category), reason.isEmpty() ? tr("not found") : QString(reason).replace('\n', ' '), false);
            return;
        }
        v->setImage(img, false);
        if (!f.zipped()) v->setProperty("imageFile", f.file);
        else v->setProperty("imageZip", f.file + '|' + f.entry);
        QString details = QString("%1x%2 (%3)").arg(img.width()).arg(img.height()).arg(f.type);
        if (index > 1) details += QString("  #%1").arg(index);
        v->setHintText(icon, ImageFinder::title(category), details, f.zipped());
    };
    if (!f.ok) {
        show({});
        return;
    }
    QPointer<MainWindow> self(this);
    QPointer<ImageView> view(v);
    QThreadPool::globalInstance()->start([self, view, gen, f, show, wanted] {
        auto stillWanted = [wanted, gen] { return wanted->load() == gen; };
        if (!stillWanted()) return;
        QImage img;
        img.loadFromData(ImageFinder::read(f, stillWanted));
        if (!self || !stillWanted()) return;
        QMetaObject::invokeMethod(self, [view, gen, img, show] {
            if (view && view->property("imageGen").toInt() == gen) show(img); // not when another game was selected since
        }, Qt::QueuedConnection);
    });
}

// DeleteRenameCurrentImage: the picture the menu was opened on.
void MainWindow::deleteRenameImage(bool rename) {
    const MameGame* g = current();
    if (!g || !popupView_) return;
    ImageDeleteRenameDialog::Input in;
    in.file = popupView_->property("imageFile").toString();
    if (in.file.isEmpty()) return;
    in.game = *g;
    in.gameIcon = model_->icon(*g);
    in.nameLine = ui_->StatusBar_GamesGameName->text();
    in.system = sys_ == SystemId::HBMAME ? QStringLiteral("HBMAME") : QStringLiteral("MAME");
    int category = popupView_->property("imageCategory").toInt();
    in.categoryIcon = toolIcon(QString(ImageFinder::category(category).icon) + ".ico", false);
    in.rename = rename;
    if (ImageDeleteRenameDialog::run(in, this)) updateImage();
}

// Menu Options > Images > Delete Images of Clone Games (MenuImagesDeleteCloneGameImagesClick)
void MainWindow::deleteCloneImages() {
    ImagesDeleteClonesDialog::Setup st;
    // the games shown (a software list game's pictures: <folder>\<list>\<name>.png)
    for (int r = 0; r < filter_->rowCount(); ++r) st.games << model_->game(filter_->mapToSource(filter_->index(r, 0)).row());
    st.finder = &imageFinder_;
    st.category = layouts_ && layouts_->layout() == 0 ? imageCategory_ : 1;
    st.system = sys_ == SystemId::HBMAME ? QStringLiteral("HBMAME") : QStringLiteral("MAME");
    st.systemIcon = QIcon(fe_.mainIcon(sys_ == SystemId::HBMAME ? "emu_hbmame.ico" : "emu_mame.ico"));
    st.categoryIcon = [this](int c) { return toolIcon(QString(ImageFinder::category(c).icon) + ".ico", false); };
    st.background = [this](int c) { return ElColorBox::toQColor(singleBack_ ? singleBackColor_ : categoryBack_[c]); };
    st.gameIcon = [this](const MameGame& g) { return model_->icon(g); };
    st.extrasIni = fe_.userFile("el_extras.ini");
    ImagesDeleteClonesDialog dlg(st, this);
    dlg.exec();
    if (dlg.changed()) updateImage();
}

// MAME Images Manager (MenuImagesManagerClick)
// The image folders (and the MAMu_ icons folders: the Images Manager's Icons category)
void MainWindow::setupImageFinder() {
    imageFinder_.setup(fe_.dataDir(), fe_.userDir(), fe_.mameFolder(sys_), fe_.mameIniPath(sys_), sys_ == SystemId::HBMAME ? "HBMAME" : "MAME");
    imageFinder_.setIconFolders(fe_.iconFolders());
    imageFinder_.setSoftwareLists(SoftwareLists::made(softwareFolder())); // where their games' pictures may be
}

void MainWindow::openImagesManager(int category) {
    ImagesManagerDialog::Setup st;
    // the games shown (a software list game's pictures: <folder>\<list>\<name>.png, scanned with
    // Scan Software List Games), and every game: their pictures aren't invalid
    for (int r = 0; r < filter_->rowCount(); ++r) st.shown << model_->game(filter_->mapToSource(filter_->index(r, 0)).row());
    st.all = QList<MameGame>(db_.games().cbegin(), db_.games().cend());
    st.finder = &imageFinder_;
    st.category = category >= 0 ? category : layouts_ && layouts_->layout() == 0 ? imageCategory_ : 1;
    st.system = sys_ == SystemId::HBMAME ? QStringLiteral("HBMAME") : QStringLiteral("MAME");
    st.categoryIcon = [this](int c) {
        return c == ImageFinder::Icons ? ui_->ButtonMAMu_Icons->icon() : toolIcon(QString(ImageFinder::category(c).icon) + ".ico", false);
    };
    st.gameIcon = [this](const MameGame& g) { return model_->icon(g); };
    st.play = [this](const QString& name) {
        if (!selectGameByName(QString(name).replace('/', '\\'))) return false; // nes/smb: the game nes\smb
        runGame();
        return true;
    };
    st.extrasIni = fe_.userFile("el_extras.ini");
    st.excludeFile = fe_.userFile("arcade/el_mamu_exclude.ini");
    st.blankIcon = fe_.resourcesFolder() + "blank_icon.ico";
    setupImageFinder();
    mamu_.release(); // icons.zip can be written again (Windows can't replace an open file)
    ImagesManagerDialog dlg(st, this);
    dlg.exec();
    setupImageFinder();
    if (mamu_.options().enabled) mamu_.setFolders(fe_.iconFolders()); // icons added, deleted or zipped
    refreshIcons();
    updateImage(); // snapshots taken while it was open, files deleted or renamed
}

// ---------------------------------------------------------- video previews

// The selected game's manual (OpenGameManualPDF's search): the button and the menu item are
// enabled when there's one; the tooltip says where it is, or where it was looked for.
void MainWindow::findManual() {
    const MameGame* g = current();
    manualFile_ = g ? fe_.gameManual(g->imageName(), g->imageParent()) : QString();
    const bool has = !manualFile_.isEmpty();
    ui_->ButtonOpenManualPDF->setEnabled(has);
    if (QAction* a = menus_.action("PopupViewGameManualPDF")) a->setEnabled(has);
    const QString tip = has ? tr("View Game Manual (PDF): %1").arg(QDir::toNativeSeparators(manualFile_))
                            : tr("No PDF manual for this game in %1").arg(QDir::toNativeSeparators(fe_.manualsFolder()));
    ui_->ButtonOpenManualPDF->setToolTip(tip);
}

void MainWindow::openManual() {
    findManual();
    if (manualFile_.isEmpty()) return;
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(manualFile_)))
        QMessageBox::warning(this, tr("View Game Manual (PDF)"),
                             tr("No application opens PDF files. Choose one in Windows for the .pdf file type, then try again.\n\n%1")
                                 .arg(QDir::toNativeSeparators(manualFile_)));
}

// Category Settings (PopupImageCategorySettingsClick)
void MainWindow::addPicturesPage(SettingsDialog& s, QVector<std::function<void()>>& after) {
    CategorySettingsDialog::Values v;
    for (int c = 0; c < ImageFinder::Count; ++c) {
        v.background[c] = categoryBack_[c];
        v.enabled[c] = categoryEnabled_[c];
    }
    v.video = videoCategoryOn_;
    if (!video_.enabled)
        v.videoOff = tr("Video previews are turned off in Settings > Video, so the Video category is left out.");
    else if (!internalVideo())
        v.videoOff = VideoPanel::available()
                         ? tr("Videos play in another media player (Settings > Video), so the Video category is left out.")
                         : tr("EmuLoader's player (%1) isn't next to EmuLoader, so the Video category is left out.")
                               .arg(VideoPanel::libraryName());
    v.single = singleBack_;
    v.singleColor = singleBackColor_;
    int category = layouts_ && layouts_->layout() == 0 ? imageCategory_ : 1;
    auto* dlg = new CategorySettingsDialog(fe_, sys_, category, v, [this](int c) {
        return toolIcon(c == CategorySettingsDialog::kVideo ? QStringLiteral("video_preview.ico")
                                                            : QString(ImageFinder::category(c).icon) + ".ico", false);
    }, &s);
    dlg->settingsLayout();
    s.addDialog(tr("Pictures"), dlg, QString(), QString());
    after << [this, dlg] {
    if (dlg->result() != QDialog::Accepted) return;
    CategorySettingsDialog::Values v = dlg->values();
    for (int c = 0; c < ImageFinder::Count; ++c) {
        categoryBack_[c] = v.background[c];
        categoryEnabled_[c] = c == 1 || v.enabled[c];
    }
    videoCategoryOn_ = v.video;
    if (!videoCategoryOn_ && imageVideo_) { // (Video isn't one of them any more)
        stopVideo();
        imageVideo_ = false;
    }
    singleBack_ = v.single;
    singleBackColor_ = v.singleColor;
    CategorySettingsDialog::save(fe_.iniFilesFolder() + "image_category.ini", v);
    if (QAction* a = menus_.action("PopupImageUseSingleBackgroundColor")) {
        QSignalBlocker b(a);
        a->setChecked(singleBack_);
    }
    if (QAction* a = menus_.action("MenuImageUseSingleBackgroundColor")) {
        QSignalBlocker b(a);
        a->setChecked(singleBack_);
    }
    // A hidden category can't stay the one shown (the single layout's)
    if (!categoryEnabled_[imageCategory_]) imageCategory_ = 1;
    updateCategoryButton();
    if (dlg->foldersChanged()) refreshImageFolders();
    else updateImage();
    };
}

// Show Layout Dimensions (TFormImageLayoutDimensions): a small panel over the games list with
// the layout's picture, the images panel's size and each picture's.
void MainWindow::showLayoutDimensions(bool on) {
    if (!on) {
        delete dimensions_;
        dimensions_ = nullptr;
        return;
    }
    if (!dimensions_) {
        // The form's panel goes over the games list (its Parent is GamesListView in the original).
        QWidget form;
        Ui::FormImageLayoutDimensions ui;
        ui.setupUi(&form);
        vcl::apply(&form);
        QWidget* panel = ui.PanelImageLayoutDimensions;
        panel->setParent(list_);
        panel->resize(panel->size());
        ui.PanelImageLayoutDimensLabel->setText(tr("Image Layout Dimensions"));
        ui.PanelImageLayoutDimensLabel->adjustSize();
        ui.PanelImageLayoutDimensButtonClose->setToolTip(tr("Close"));
        ui.PanelImageLayoutDimensButtonClose->setCursor(Qt::PointingHandCursor);
        ui.PanelImageLayoutDimensButtonClose->installEventFilter(this);
        ui.LabelImagesPanel->setText(tr("Images Panel:"));
        for (QLabel* l : {ui.LabelImagesPanel, ui.LabelImagesPanelDimensions}) l->setStyleSheet("color: #FF7070;"); // clrLightRed
        dimensions_ = panel;
        panel->show();
    }
    updateLayoutDimensions();
}

// UpdateImageDimensionsInfo + ImageLayoutDimensionsUpdatePos
void MainWindow::updateLayoutDimensions() {
    if (!dimensions_ || !layouts_) return;
    auto label = [this](const char* name) { return dimensions_->findChild<QLabel*>(name); };
    int layout = layouts_->layout();
    if (QLabel* p = label("ImageScrLayout")) {
        QPixmap pm(fe_.resourcesFolder() + "images/img_layouts/" + ImageLayouts::iconFile(layout) + ".png");
        p->setPixmap(pm.isNull() ? pm : pm.scaled(p->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        p->setAlignment(Qt::AlignCenter);
    }
    if (QLabel* t = label("LabelLayoutTitle")) {
        t->setText(ImageLayouts::title(layout));
        t->setAlignment(Qt::AlignCenter);
    }
    QSize panel = ui_->PanelImage->contentsRect().size();
    QLabel* pt = label("LabelImagesPanel");
    QLabel* pv = label("LabelImagesPanelDimensions");
    // As wide as the text first: a label narrower than its text is shortened with "..."
    auto fit = [](QLabel* l, const QString& text) {
        l->resize(l->fontMetrics().horizontalAdvance(text) + 4, l->height());
        l->setText(text);
    };
    if (pt && pv) {
        fit(pt, tr("Images Panel:"));
        fit(pv, tr("%1x%2 pixels").arg(panel.width()).arg(panel.height()));
        int w = pt->width() + 6 + pv->width();
        int x = (dimensions_->width() - w) / 2;
        vcl::place(pt, QRect(x, pt->y(), pt->width(), pt->height()));
        vcl::place(pv, QRect(x + pt->width() + 6, pt->y(), pv->width(), pt->height()));
    }
    for (int i = 1; i <= 4; ++i) {
        QLabel* title = label(QString("LabelImage%1").arg(i).toLatin1());
        QLabel* value = label(QString("LabelImage%1Dimensions").arg(i).toLatin1());
        if (!title || !value) continue;
        ImageView* v = layouts_->view(i);
        bool shown = i <= ImageLayouts::imageCount(layout);
        title->setVisible(shown);
        value->setVisible(shown);
        fit(title, tr("Image %1:").arg(i));
        fit(value, v ? tr("%1x%2 pixels").arg(v->width()).arg(v->height()) : tr("Disabled"));
        vcl::place(value, QRect(title->geometry().right() + 8, title->y(), value->width(), title->height()));
    }
    // Over the games list's top right corner, under its header (ImageLayoutDimensionsUpdatePos)
    int sb = list_->verticalScrollBar()->isVisible() ? list_->verticalScrollBar()->width() : 0;
    dimensions_->move(list_->width() - dimensions_->width() - 15 - sb, list_->header()->height() + 15);
    dimensions_->raise();
}

// ------------------------------------------------------------ image layouts

void MainWindow::loadLayoutSettings() {
    WinIni lay;
    lay.load(fe_.iniFilesFolder() + "screenshot_layouts.ini");
    useConsComp_ = ImageLayouts::useConsComp(lay);
    bool any = false;
    for (int l = 0; l < ImageLayouts::Count; ++l) any |= layoutVisible_[l] = lay.readBool(ImageLayouts::section(l), "visible", true);
    if (!any) layoutVisible_[0] = true; // (Settings > Layouts keeps one: Single, if a file has none)
}

void MainWindow::setImageLayout(int layout, bool saveOld) {
    QString file = fe_.iniFilesFolder() + "screenshot_layouts.ini";
    WinIni lay;
    lay.load(file);
    if (saveOld && layouts_->layout() != 0) { // UpdateScreenshotLayout of the layout being left
        layouts_->writeSizes(lay);
        lay.save(file);
    }
    layout = std::clamp(layout, 0, ImageLayouts::Count - 1);
    layoutSettings_ = ImageLayouts::read(lay, layout, ui_->PanelImage->width(), ui_->PanelImage->height());
    stopVideo(); // (its panel goes with the old layout's)
    layouts_->build(layout, layoutSettings_, [this] { return newImageView(ui_->PanelImage); });
    QTimer::singleShot(0, this, [this] { updateLayoutDimensions(); });
    styleSplitters();
    slotIndex_.fill(1);
    // Category buttons only in the single image layout (SetImageCategoryButtonsVisible).
    bool single = layout == 0;
    for (QWidget* b : {(QWidget*)ui_->ButtonPreviousCategory, (QWidget*)ui_->ButtonImageCategory, (QWidget*)ui_->ButtonNextCategory})
        b->setVisible(single);
    for (const char* n : {"PopupImageCenterAllSplitters", "PopupImageShowSplitterGrip", "PopupImageCustomizeSplitters"})
        if (QAction* a = menus_.action(n)) a->setVisible(!single);
    layoutImageToolBar();
    QIcon ic = toolIcon(ImageLayouts::iconFile(layout) + ".ico", false);
    for (const QSize& sz : ic.availableSizes()) ic.addPixmap(ic.pixmap(sz), QIcon::Disabled);
    ui_->ButtonScreenshotLayouts->setIcon(ic);
    ui_->ButtonScreenshotLayouts->setToolTip(tr("Layout -> %1 <-\nClick to select another").arg(ImageLayouts::title(layout)));
    updateImage();
    videoPanelsChanged(); // a Video panel in the new layout plays
}

void MainWindow::cycleLayout(bool forward) {
    // CycleImageLayout: the next layout that's shown (Layout Settings > visible)
    int l = layouts_->layout();
    for (int n = 0; n < ImageLayouts::Count; ++n) {
        l = (l + (forward ? 1 : ImageLayouts::Count - 1)) % ImageLayouts::Count;
        if (layoutVisible_[l]) break;
    }
    if (l != layouts_->layout()) setImageLayout(l, true);
}

void MainWindow::selectImageLayout() {
    // FormImageLayoutSelector, under the layout button
    QDialog dlg(this, Qt::Tool);
    Ui::FormImageLayoutSelector ui;
    ui.setupUi(&dlg);
    vcl::apply(&dlg);
    dlg.setWindowTitle(tr("Image Layout"));
    auto* model = new QStandardItemModel(&dlg);
    QString dir = fe_.resourcesFolder() + "images/img_layouts/";
    for (int l = 0; l < ImageLayouts::Count; ++l) {
        if (!layoutVisible_[l]) continue;
        auto* it = new QStandardItem(QIcon(dir + ImageLayouts::iconFile(l) + ".png"), ImageLayouts::title(l));
        it->setData(l);
        model->appendRow(it);
    }
    auto* view = ui.LayoutsListView;
    view->setModel(model);
    view->setRootIsDecorated(false);
    view->setHeaderHidden(true);
    view->setIconSize(QSize(64, 48));
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int r = 0; r < model->rowCount(); ++r)
        if (model->item(r)->data().toInt() == layouts_->layout()) view->setCurrentIndex(model->index(r, 0));
    connect(ui.ButtonOk, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(ui.ButtonCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(view, &QTreeView::doubleClicked, &dlg, &QDialog::accept);
    ui.LabelLayoutTitle->adjustSize();
    dlg.move(popupPos(ui_->ButtonScreenshotLayouts, dlg.size()));
    if (dlg.exec() != QDialog::Accepted) return;
    QModelIndex i = view->currentIndex();
    if (i.isValid() && model->itemFromIndex(i)->data().toInt() != layouts_->layout())
        setImageLayout(model->itemFromIndex(i)->data().toInt(), true);
}

// Menu Options > Images > Layout Settings
void MainWindow::addLayoutsPage(SettingsDialog& s, QVector<std::function<void()>>& after) {
    QString file = fe_.iniFilesFolder() + "screenshot_layouts.ini";
    { // the current sizes first, so the dialog doesn't overwrite them with older ones
        WinIni lay;
        lay.load(file);
        layouts_->writeSizes(lay);
        lay.writeInteger("Single", "image1_category", imageCategory_);
        lay.writeInteger("Single", "image1_video", imageVideo_ ? 1 : 0);
        lay.save(file);
    }
    auto* dlg = new LayoutSettingsDialog(file, fe_.resourcesFolder(), layouts_->layout(),
                                         [this](int cur) { return pickCategory(cur, nullptr); }, &s);
    dlg->settingsLayout();
    s.addDialog(tr("Layouts"), dlg, QString(), QString());
    after << [this, dlg, file] {
        if (dlg->result() != QDialog::Accepted) return;
        WinIni lay;
        lay.load(file);
        imageCategory_ = std::clamp(lay.readInteger("Single", "image1_category", 1), 0, ImageFinder::Count - 1);
        imageVideo_ = lay.readBool("Single", "image1_video", false);
        if (imageVideo_) imageCategory_ = 1;
        updateCategoryButton();
        loadLayoutSettings();
        // the layout selected on the tab is the one used
        const int chosen = dlg->chosen();
        setImageLayout(chosen, chosen != layouts_->layout());
    };
}

// UpdateImageLayoutSplittersStyle: Preferences page 2 "Image Splitters".
void MainWindow::styleSplitters() {
    for (ElSplitter* s : layouts_->splitters()) {
        s->setTmsStyle(lightMode_.imgSplitterStyle, LightMode::qc(lightMode_.imgSplitterSingle),
                       LightMode::qc(lightMode_.imgSplitterSingleHot));
        s->setGrip(lightMode_.imgSplitterGrip);
        // the splitter's own menu (PopupSplitters): Center This Splitter and the image menu's splitter items
        if (!s->property("elMenu").toBool()) {
            s->setProperty("elMenu", true);
            s->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(s, &QWidget::customContextMenuRequested, this, [this, s](const QPoint& p) {
                QMenu* m = menus_.menu("PopupSplitters");
                if (!m) return;
                splitterForMenu_ = s;
                if (QAction* a = menus_.action("PopupShowSplitterGrip")) a->setChecked(lightMode_.imgSplitterGrip);
                m->exec(s->mapToGlobal(p));
            });
        }
    }
    if (QAction* a = menus_.action("PopupImageShowSplitterGrip")) a->setChecked(lightMode_.imgSplitterGrip);
}

// Lays the image tool bar buttons out left to right, skipping hidden ones.
void MainWindow::layoutImageToolBar() {
    int ix = 2;
    for (ElButton* b : ui_->ImagesToolBarButtons->findChildren<ElButton*>()) {
        if (b->isHidden() || b->property("vclSeparator").toBool()) { if (!b->isHidden()) ix += 8; continue; }
        vcl::place(b, QRect(ix, 2, 27, 27));
        ix += 27;
    }
}

// ---------------------------------------------------------- games list I/O

void MainWindow::createGamesList() {
    QString exe = fe_.emulatorFile(sys_);
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        QMessageBox::warning(this, tr("Create Arcade Games List"), tr("Set up %1 first (Menu Options > MAME/Arcade > Emulators Setup).").arg(Frontend::section(sys_)));
        return;
    }
    // Progress (the original's status screen): MAME's list, the ROM folders, saving. MAME doesn't
    // say how many machines it will send, so the first step counts them as they come.
    ProgressWindow win(tr("Create Arcade Games List"), this);
    const QLocale loc;
    win.step(tr("Asking %1 for its machine list (-listxml)...").arg(Frontend::section(sys_)));
    QProcess p;
    p.setWorkingDirectory(fe_.mameFolder(sys_));
    commandPromptBox(&p, fe_.ini().readBool("Configuration", "HideDOSBoxEmuVersionCreateGames", true), true);
    p.start(exe, {"-listxml"});
    DiagnosticLog::write("process", "Launching: " + QDir::toNativeSeparators(exe) + " -listxml (Create Arcade Games List)");
    if (!p.waitForStarted()) {
        DiagnosticLog::write("error", "Could not start " + QDir::toNativeSeparators(exe) + ": " + p.errorString());
        win.close();
        FileProblem::cantStart(this, tr("Create Arcade Games List"), exe, p.errorString());
        return;
    }
    std::atomic<bool> cancel{false};
    ElDatabase fresh;
    QByteArray pending;
    int machines = 0;
    bool ok = fresh.fromListXml(
        [&](QString& line) {
            while (true) {
                int nl = pending.indexOf('\n');
                if (nl >= 0) {
                    line = QString::fromUtf8(pending.left(nl));
                    pending.remove(0, nl + 1);
                    return true;
                }
                if (win.canceled()) { cancel = true; p.kill(); return false; }
                if (p.state() == QProcess::NotRunning && p.bytesAvailable() == 0) {
                    if (pending.isEmpty()) return false;
                    line = QString::fromUtf8(pending);
                    pending.clear();
                    return true;
                }
                p.waitForReadyRead(100);
                pending += p.readAllStandardOutput();
                win.setProgress(machines); // keeps the window drawn while MAME is busy
            }
        },
        [&](int n) {
            machines = n;
            win.setProgress(n, tr("Found %1 machines").arg(loc.toString(n)));
        },
        &cancel);
    p.waitForFinished();
    DiagnosticLog::write("process", QString("-listxml finished; exit code=%1; machines=%2; canceled=%3").arg(p.exitCode()).arg(machines).arg(cancel ? "yes" : "no"));
    if (!ok || cancel) {
        win.close();
        if (!cancel)
            QMessageBox::warning(this, tr("Create Arcade Games List"),
                                 tr("%1 didn't give a list of machines, so the games list wasn't changed.").arg(QFileInfo(exe).fileName()) + "\n\n" +
                                     tr("Check that the file set up as MAME (%1) is MAME, and run it once from a command prompt to see "
                                        "whether it reports an error.").arg(QDir::toNativeSeparators(exe)));
        return; // the current list stays as it was
    }
    win.setCancelable(false); // from here on, the new list is being put in place
    win.step(tr("Checking the ROM folders..."));
    fresh.quickScan(fe_.romFolders(sys_));
    win.step(tr("Saving the games list (%1 machines)...").arg(loc.toString(int(fresh.games().size()))));
    fresh.description = (sys_ == SystemId::MAME ? "MAME" : "HBMAME") + QString(" Games List");
    fresh.version = fe_.emulatorVersion(sys_);
    if (!fresh.save(fe_.databaseFile(sys_), fe_.statusFile(sys_)))
        FileProblem::cantSave(this, tr("Create Arcade Games List"), fe_.databaseFile(sys_));
    fresh.saveBiosSets(fe_.biosSetsFile(sys_)); // BIOS choices in the game settings window
    fresh.saveSoftwareLists(fe_.softwareListsFile(sys_));
    fresh.saveSamples(fe_.samplesFile(sys_)); // Scan Missing Audio Samples
    fresh.saveRamSizes(fe_.gamesFolder() + Frontend::fileBase(sys_) + "_ramsize.ini"); // MAME Settings > RAM Size
    MemoryCards::write(MemoryCards::supportFile(fe_.gamesFolder(), Frontend::fileBase(sys_)), fresh.memoryCards);
    const QString hash = SoftwareLists::hashFolder(fe_.mameIniPath(sys_), fe_.mameFolder(sys_));
    // The games a multi-slot Neo-Geo MVS and ST-V take, from MAME's lists of their cartridges
    // (CreateSoftwareListXML(..., IsMultiSlot)): hash\neogeo.xml, hash\stv.xml
    for (const QString& m : {QStringLiteral("neogeo"), QStringLiteral("stv")}) {
        const QString file = MultiSlot::cartFile(fe_.gamesFolder(), Frontend::fileBase(sys_), m);
        QFile::remove(file);
        if (const QString xml = QDir(hash).filePath(m + ".xml"); QFileInfo::exists(xml)) MultiSlot::createCartList(xml, file);
    }
    // The software list games with it, unless that's turned off ([Preferences] CreateMAMESoftwareListGames:
    // 0 off, 1 update, 2 overwrite; Create MAME Software List Games asks).
    const int softMode = fe_.ini().readInteger("Preferences", "CreateMAMESoftwareListGames", 1);
    if (softMode != 0 && QFileInfo(hash).isDir() && !softwareListNames(fresh).isEmpty()) {
        win.setCancelable(true); // the machines' list is saved: the software lists can be left for later
        makeSoftwareLists(win, softMode == 2 ? 2 : 1, fresh);
        win.setCancelable(false);
        win.step(tr("Checking the ROM folders..."));
        QVector<MameGame> games = SoftwareLists::loadAll(softwareFolder(), SoftwareLists::excluded(softwareExcludeFile()));
        scanSoftware(games);
    }
    win.step(tr("Loading the new games list..."));
    loadGamesList();
    win.close();
}

void MainWindow::rescanRoms() {
    if (db_.games().isEmpty()) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    db_.quickScan(fe_.romFolders(sys_));
    DiagnosticLog::write("games", "ROM folders scanned again: " + QDir::toNativeSeparators(fe_.romFolders(sys_).join(';')));
    db_.saveStatus(fe_.statusFile(sys_)); // the list itself (mame.el) doesn't change
    if (db_.softwareCount()) scanSoftware(db_.games());
    QByteArray layout = list_->header()->saveState();
    model_->reload();
    list_->header()->restoreState(layout);
    QApplication::restoreOverrideCursor();
    updateStatus();
}

// ---------------------------------------------------------------- emulator

// Menu Options > MAME/Arcade > Emulators Setup (MenuArcadeEmulatorSetupClick)
void MainWindow::addEmulatorsPage(SettingsDialog& s, QVector<std::function<void()>>& after) {
    const EmulatorInfo before = fe_.emulator(sys_);
    EmulatorsSetupDialog::Hooks hooks;
    hooks.prepare = [this](QProcess* p) {
        commandPromptBox(p, fe_.ini().readBool("Configuration", "HideDOSBoxEmuVersionCreateGames", true), true);
    };
    hooks.openSettings = [this](SystemId s) {
        if (!QFileInfo::exists(fe_.mameIniPath(s))) {
            QMessageBox::warning(this, tr("Emulators Default Settings"),
                                 tr("There's no mame.ini, and MAME didn't make one when asked (mame -createconfig). Check that MAME "
                                    "is set up, and that its folder isn't read-only."));
            return;
        }
        MameSettingsDialog dlg(fe_, s, this);
        dlg.exec();
    };
    auto* dlg = new EmulatorsSetupDialog(fe_, sys_, hooks, &s);
    dlg->settingsLayout();
    s.addDialog(tr("Emulators"), dlg, QString(), QString());
    after << [this, dlg, before] {
    if (dlg->result() != QDialog::Accepted) return;
    // The list shows the system with an emulator (MAME first, as at start-up).
    if (!fe_.emulator(sys_).isSet()) {
        SystemId other = sys_ == SystemId::MAME ? SystemId::HBMAME : SystemId::MAME;
        if (fe_.emulator(other).isSet()) {
            sys_ = other;
            db_ = ElDatabase{};
            model_->reload();
            loadFavorites();
            loadSpecial();
            loadGamesList();
        }
    }
    const EmulatorInfo now = fe_.emulator(sys_);
    if (!now.isSet()) return;
    if (db_.games().isEmpty())
        askCreateGamesList(true);
    else if (before.isSet() && before.build != now.build && !db_.games().isEmpty())
        ui_->StatusBar_GamesGameName->setText(
            tr("The emulator changed from %1 to %2: create the games list again to update it (Menu Options > MAME/Arcade).")
                .arg(before.build, now.build));
    refreshImageFolders();
    };
}

void MainWindow::openEmulatorSettings(const QString& reveal) {
    if (!QFileInfo::exists(fe_.mameIniPath(sys_))) {
        QMessageBox::warning(this, tr("Emulators Default Settings"),
                             tr("There's no mame.ini yet. Set up %1 first (Menu Options > MAME/Arcade > Emulators Setup).").arg(Frontend::section(sys_)));
        return;
    }
    MameSettingsDialog dlg(fe_, sys_, this);
    if (!reveal.isEmpty()) dlg.reveal(reveal);
    dlg.exec();
}

void MainWindow::openCustomSettings(int level, bool remove, const std::function<void(MameGame&)>& adjust) {
    const MameGame* cur = current();
    if (!cur || !QFileInfo::exists(fe_.mameIniPath(sys_))) return;
    MameGame g = *cur;
    if (adjust) adjust(g);
    QString path = MameIniChain::path(level, fe_.chainMachine(g), fe_.mameIniPath(sys_), fe_.iniDir(sys_), fe_.mameBuild(sys_));
    if (path.isEmpty()) {
        QMessageBox::information(this, tr("Custom Settings"), tr("This setting level doesn't apply to %1.").arg(g.name));
        return;
    }
    if (remove) {
        if (!QFileInfo::exists(path)) {
            QMessageBox::information(this, tr("Custom Settings"), QDir::toNativeSeparators(path) + "\ndoes not exist.");
            return;
        }
        if (QMessageBox::question(this, tr("Custom Settings"), tr("Delete %1?").arg(QDir::toNativeSeparators(path))) == QMessageBox::Yes)
            QFile::remove(path);
        return;
    }
    MameSettingsDialog dlg(fe_, sys_, this);
    dlg.setCustom(g, level);
    dlg.exec();
}

// ExecuteGame: the game with the system's emulator (MAME or HBMAME), from its folder. When it stops
// with an error, a window says why (showGameFailed). (The original could then run it again with
// AlterMAME, an older MAME build; that's gone: today's ROM sets no longer match an old build's.)
void MainWindow::runGame(const QStringList& extra) {
    const MameGame* g = current();
    if (!g || running_) return;
    struct Emu { QString exe, title, folder; };
    const QString exe = fe_.emulatorFile(sys_);
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        QMessageBox::warning(this, tr("Run Game"), tr("%1 is not set up (Menu Options > MAME/Arcade > Emulators Setup).").arg(Frontend::section(sys_)));
        return;
    }
    const Emu emu{exe, Frontend::section(sys_), fe_.mameFolder(sys_)};
    QString name = g->name;
    const QString key = g->key();
    // CustomParam_ReadFile (MAME only): the game's custom parameters, else its parent's.
    QStringList args{name};
    QStringList slotArgs;
    if (g->isSoftware()) {
        // a software list game: the machine, its requirement, then the game
        args = softwareArgs(*g);
        if (args.isEmpty()) return;
    } else if (multiSlotArgs(*g, &slotArgs)) {
        // several Neo-Geo (ST-V ...) games selected: one machine, a game per slot
        if (slotArgs.isEmpty()) return; // Cancel
        args = slotArgs;
    } else if (sys_ == SystemId::MAME) {
        QString f = CustomParams::fileFor(CustomParams::folder(fe_.userDir(), Frontend::fileBase(sys_)), name, g->cloneOf);
        QString params = f.isEmpty() ? QString() : CustomParams::read(f);
        if (!params.isEmpty()) args = CustomParams::arguments(name, params);
    }
    args << extra; // Run Game (Extra)
    if (!confirmRun(*g, emu.exe, args)) return;
    stopVideo();
    playDummyVideo(true); // stops the video that's playing
    fe_.ini().writeString("Selected", "LastSelected", key);
    // While a game runs EmuLoader minimizes itself, unless Preferences > DisableMinimize.
    stateBeforeGame_ = windowState();
    auto clock = std::make_shared<QElapsedTimer>();
    const QString title = g->title;
    {
        auto* p = new QProcess(this);
        running_ = p;
        hideGameNotice(); // (about the game played before)
        p->setWorkingDirectory(emu.folder);
        // MAME's error output is read (why it stopped: the missing ROM files ...), the rest goes on
        p->setProcessChannelMode(QProcess::ForwardedOutputChannel);
        commandPromptBox(p, fe_.ini().readBool("Configuration", "HideDOSBoxRunGame", false), true);
        auto errors = std::make_shared<QByteArray>();
        connect(p, &QProcess::readyReadStandardError, this, [p, errors] {
            errors->append(p->readAllStandardError());
            if (errors->size() > 256 * 1024) errors->remove(0, errors->size() - 128 * 1024); // the end of it
        });
        connect(p, &QProcess::started, this, [clock] { if (!clock->isValid()) clock->start(); });
        connect(p, &QProcess::finished, this, [this, p, emu, name, title, key, clock, errors](int code, QProcess::ExitStatus st) {
            errors->append(p->readAllStandardError());
            p->deleteLater();
            DiagnosticLog::write("process", QString("Process finished; exit code=%1%2; played %3 s")
                                                .arg(code).arg(st == QProcess::CrashExit ? " (crashed)" : "")
                                                .arg(clock->isValid() ? clock->elapsed() / 1000 : 0));
            const bool ignore1 = code == 1 && st == QProcess::NormalExit &&
                                 fe_.ini().readBool("Preferences", "IgnoreExitCode1InvalidFunction", true);
            const bool failed = (st == QProcess::CrashExit || code != 0) && !ignore1;
            const GameAttempt attempt{emu.title, code, st == QProcess::CrashExit, QString::fromLocal8Bit(*errors).trimmed()};
            if (!attempt.message.isEmpty()) DiagnosticLog::write("process", emu.title + " said: " + attempt.message.right(2000));
            running_ = nullptr;
            // UpdatePlayedGameArcade: one more play, whatever the exit code.
            PlayStat ps = played_.record(playedFile(sys_), key, clock->isValid() ? clock->elapsed() : 0,
                                         QDateTime::currentDateTime());
            if (int r = db_.indexOf(key); r >= 0) {
                MameGame& m = db_.games()[r];
                m.played = ps.times;
                m.lastPlayed = ps.lastPlayed;
                m.playtime = ps.playtime;
                model_->refreshRow(r);
            }
            // Back to how the window was before the game (maximized stays maximized).
            setWindowState(stateBeforeGame_ & ~Qt::WindowMinimized);
            show();
            raise();
            activateWindow();
            // FocusGamesList: only the played game selected, and the list drawn afresh. When MAME
            // hangs or is killed, the window can come back showing selection bars on several rows
            // (Windows putting back an old picture of the list while it restores the screen
            // resolution and the maximized window): do it again once that has settled.
            auto refocus = [this, key] {
                QModelIndexList rows = list_->selectionModel()->selectedRows();
                const MameGame* cur = current();
                if (rows.size() != 1 || !cur || cur->key() != key) selectGameByName(key);
                else gamesView()->scrollTo(list_->currentIndex(), QAbstractItemView::EnsureVisible); // in view
                gamesView()->setFocus();
                fullRepaint();
            };
            refocus();
            QTimer::singleShot(300, this, refocus);
            if (failed) {
                const QHash<int, QString> meaning = {{1, tr("failed validity checks")}, {2, tr("missing ROM files")},
                                                     {3, tr("fatal error")}, {4, tr("device/image error")},
                                                     {5, tr("machine not found")}, {6, tr("invalid configuration")}};
                ui_->StatusBar_GamesGameName->setText(tr("%1: %4 exited with code %2 %3").arg(name).arg(code).arg(meaning.value(code), emu.title));
                // after the window is back and the status bar drawn
                QTimer::singleShot(0, this, [this, title, name, attempt] { showGameFailed(title, name, attempt); });
            } else if (!attempt.message.isEmpty()) { // it ran, and said something (artwork it couldn't load ...)
                noteGameWarnings(title, name, attempt);
                QTimer::singleShot(350, this, [this, title, name, attempt] { noteGameWarnings(title, name, attempt); }); // (after refocus)
            }
        });
        connect(p, &QProcess::errorOccurred, this, [this, p, emu](QProcess::ProcessError e) {
            DiagnosticLog::write("error", emu.title + " process error " + QString::number(int(e)) + ": " + p->errorString());
            if (e == QProcess::FailedToStart) {
                running_ = nullptr;
                const QString reason = p->errorString();
                p->deleteLater();
                setWindowState(stateBeforeGame_ & ~Qt::WindowMinimized); // (no game is running)
                show();
                QTimer::singleShot(0, this, [this, emu, reason] { showGameNotStarted(emu.title, emu.exe, reason); });
            }
        });
        DiagnosticLog::write("process", "Launching: " + QDir::toNativeSeparators(emu.exe) + " " + args.join(' ') + "; game=" + key);
        p->start(emu.exe, args);
    }
    if (running_ && !fe_.ini().readBool("Preferences", "DisableMinimize", false)) showMinimized();
}

// ValidateMultiSlot: with several games selected, the game a Neo-Geo MVS, ST-V, Mega-Tech or
// PlayChoice-10 game: the ones of them that go in its slots, put in order in Multi-Slot
// Machines, then "<machine> -cart1 <game> -cart2 <game> ...". False when that isn't the case (the
// game runs on its own); true with no arguments when the window was closed (nothing runs).
bool MainWindow::multiSlotArgs(const MameGame& g, QStringList* args) {
    QModelIndexList rows = list_->selectionModel()->selectedRows();
    if (rows.size() < 2) return false;
    const QString machine = MultiSlot::machineOf(g);
    if (machine.isEmpty()) return false;
    const QString base = Frontend::fileBase(sys_);
    QSet<QString> carts;
    if (MultiSlot::hasCartList(machine)) {
        const QString file = MultiSlot::cartFile(fe_.gamesFolder(), base, machine);
        if (!QFileInfo::exists(file)) return false; // MAME's list of its cartridges wasn't there
        carts = MultiSlot::loadCartList(file);
    }
    std::sort(rows.begin(), rows.end(), [](const QModelIndex& a, const QModelIndex& b) { return a.row() < b.row(); });
    QVector<MameGame> selected;
    for (const QModelIndex& i : rows) selected << model_->game(filter_->mapToSource(i).row());
    const QVector<MameGame> games = MultiSlot::slotGames(machine, selected, carts);
    if (games.size() < 2) return false;
    MultiSlotDialog::Input in;
    in.machine = machine;
    in.games = games;
    // the Neo-Geo boards: this version's file, else EmuLoader's (the last one used is written to ours)
    const QString mine = fe_.userFile("arcade/" + base + "_multislot_machines.txt");
    const QString theirs = QDir(fe_.dataDir()).filePath("arcade/" + base + "_multislot_machines.txt");
    const QString boards = QFileInfo::exists(mine) ? mine : theirs;
    if (machine == QLatin1String("neogeo")) {
        in.boards = MultiSlot::boards(boards);
        in.board = MultiSlot::lastBoard(boards);
    }
    in.icon = [this](const MameGame& m) { return model_->icon(m); };
    in.players = [this](const MameGame& m) { return model_->playersText(m); };
    MultiSlotDialog dlg(in, this);
    args->clear();
    if (dlg.exec() != QDialog::Accepted) return true;
    if (!dlg.board().isEmpty() && dlg.board() != in.board) MultiSlot::setLastBoard(mine, dlg.board(), theirs);
    *args = MultiSlot::commandLine(MultiSlot::machineToRun(machine, dlg.board()), dlg.games());
    return true;
}

// Menu Options > Run Game Confirmation Dialog ([Preferences] ShowEmulatorFullCommandLine): before
// a game runs, a window shows the emulator and MAME's full command line, with Run and Cancel.
// "Don't Show This Again" turns the menu item off (TFormMessageBox.HideCommandLineRunConfirm).
bool MainWindow::confirmRun(const MameGame& g, const QString& exe, const QStringList& args) {
    if (!fe_.ini().readBool("Preferences", "ShowEmulatorFullCommandLine", false)) return true;
    auto quoted = [](QString a) {
        a = QDir::toNativeSeparators(a);
        return a.contains(' ') ? '"' + a + '"' : a;
    };
    QStringList line{quoted(exe)};
    for (const QString& a : args) line << (a.contains(' ') ? '"' + a + '"' : a);
    // the game and emulator lines centered, as the original, unless Preferences > Left Align
    // Emulator Game Text Message Box (GetAlignEmuGameText)
    const QString align = fe_.ini().readBool("Preferences", "LeftAlignEmulatorGameTextMessageBox", false) ? "left" : "center";
    QString html = QString("<p align=\"%3\"><b>%1</b><br>%2</p>").arg(g.title.toHtmlEscaped(), Frontend::section(sys_) + ": " + g.key(), align);
    const QString version = fe_.emulator(sys_).version;
    html += QString("<p align=\"%3\"><b>%1</b>&nbsp;&nbsp;%2</p>").arg(tr("Emulator"), version.toHtmlEscaped(), align);
    html += QString("<p style=\"font-family:Consolas,monospace\">run&gt;%1</p>").arg(line.join(' ').toHtmlEscaped());
    if (g.setStatus > 0)
        html += QString("<p style=\"color:#c00000\"><i>%1</i></p>")
                    .arg(tr("WARNING: One or more ROMs/CHDs are missing, \"%1\" might not run properly!").arg(g.name).toHtmlEscaped());
    QMessageBox box(QMessageBox::Question, tr("Run Game: %1").arg(Frontend::section(sys_)), html, QMessageBox::NoButton, this);
    box.setTextFormat(Qt::RichText);
    QPushButton* run = box.addButton(tr("Run"), QMessageBox::YesRole);
    box.addButton(tr("Cancel"), QMessageBox::NoRole);
    box.setDefaultButton(run);
    auto* hide = new QCheckBox(tr("Don't Show This Again"), &box);
    hide->setToolTip(tr("To view the full command line again\nenable \"Run Game Confirmation Dialog\" in main menu"));
    box.setCheckBox(hide);
    box.exec();
    if (box.clickedButton() != run) return false;
    if (hide->isChecked()) {
        fe_.ini().writeInteger("Preferences", "ShowEmulatorFullCommandLine", 0);
        if (QAction* a = menus_.action("MenuViewEmulatorFullCommandLine")) a->setChecked(false);
    }
    return true;
}

// Games list menu > Run Game (Extra) (PopupPlayGameExtraParametersMAMEClick > RunMAMEExtraParameters):
// the window first, then the game with what was picked there; Cancel runs nothing.
void MainWindow::runGameExtra() {
    const MameGame* g = current();
    if (!g || running_) return;
    QString exe = fe_.emulatorFile(sys_);
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        QMessageBox::warning(this, tr("Run Game"), tr("%1 is not set up (Menu Options > MAME/Arcade > Emulators Setup).").arg(Frontend::section(sys_)));
        return;
    }
    RunGameExtraDialog::Input in;
    in.game = *g;
    in.gameIcon = model_->icon(*g);
    in.emuIcon = fe_.mainIcon(sys_ == SystemId::HBMAME ? "emu_hbmame.ico" : "emu_mame.ico");
    in.nameLine = ui_->StatusBar_GamesGameName->text();
    RunGameExtraDialog dlg(fe_, sys_, in, this);
    if (dlg.exec() != QDialog::Accepted) return;
    runGame(dlg.arguments());
}

// PopupCustomParametersClick: "Set Game Parameters [pacman]"; Delete only when there's a file;
// the parent's items only for clones. MAME only, not for devices (as the original). A software
// list game also has its list's parameters and those of the machine it runs with.
void MainWindow::prepareCustomParamsMenu(const MameGame& g) {
    bool single = list_->selectionModel()->selectedRows().size() <= 1;
    if (QAction* a = menus_.action("PopupCustomParameters"))
        a->setVisible(single && sys_ == SystemId::MAME && g.kind != SetKind::Device);
    auto set = [&](const char* item, const QString& text, ParamsFor what, bool del) {
        QAction* a = menus_.action(item);
        if (!a) return;
        const QString file = customParamsFile(g, what);
        const QString name = QFileInfo(file).completeBaseName();
        a->setText(text.arg(name));
        a->setToolTip(QDir::toNativeSeparators(QDir(CustomParams::folder(fe_.userDir(), Frontend::fileBase(sys_))).relativeFilePath(file)));
        a->setVisible(!file.isEmpty() && (!del || QFileInfo::exists(file)));
    };
    set("PopupEditGameCustomParameters", tr("Set Game Parameters [%1]"), ParamsGame, false);
    set("PopupDeleteGameCustomParameters", tr("Delete Game Parameters [%1]"), ParamsGame, true);
    set("PopupEditParentGameCustomParameters", tr("Set Parent Game Parameters [%1]"), ParamsParent, false);
    set("PopupDeleteParentGameCustomParameters", tr("Delete Parent Game Parameters [%1]"), ParamsParent, true);
    set("PopupEditSoftwareListCustomParameters", tr("Set Software List Parameters [%1]"), ParamsSoftwareList, false);
    set("PopupDeleteSoftwareListCustomParameters", tr("Delete Software List Parameters [%1]"), ParamsSoftwareList, true);
    set("PopupEditMAMEMachineCustomParameters", tr("Set MAME Machine Parameters [%1]"), ParamsMachine, false);
    set("PopupDeleteMAMEMachineCustomParameters", tr("Delete MAME Machine Parameters [%1]"), ParamsMachine, true);
    if (QAction* a = menus_.action("PopupPlayGameExtraParametersMAME"))
        a->setVisible(g.kind != SetKind::Device && g.kind != SetKind::Bios);
}

// The parameters file: custom_params\mame\<name>.txt; a software list game's are in a folder
// named after its list (<list>\<name>.txt, <list>\<list>.txt), except the machine's.
QString MainWindow::customParamsFile(const MameGame& g, ParamsFor what) const {
    const QDir d(CustomParams::folder(fe_.userDir(), Frontend::fileBase(sys_)));
    const QString sub = g.isSoftware() ? g.softwareList + "/" : QString();
    switch (what) {
    case ParamsGame: return d.filePath(sub + g.name + ".txt");
    case ParamsParent: return g.cloneOf.isEmpty() ? QString() : d.filePath(sub + g.cloneOf + ".txt");
    case ParamsSoftwareList: return g.isSoftware() ? d.filePath(sub + g.softwareList + ".txt") : QString();
    case ParamsMachine: {
        const QString m = g.isSoftware() ? SoftwareLists::lastMachine(machinesUsedFile(), g.softwareList) : QString();
        return m.isEmpty() ? QString() : d.filePath(m + ".txt");
    }
    }
    return {};
}

// The tool bar's Parameters button: the selected game's custom parameters. MAME only, one game,
// not a device, as the games list's menu.
void MainWindow::customParametersButton() {
    const MameGame* g = current();
    const bool single = list_->selectionModel()->selectedRows().size() <= 1;
    if (!g || !single || sys_ != SystemId::MAME || g->kind == SetKind::Device) {
        QMessageBox::information(this, tr("Custom Parameters"),
                                 tr("Custom parameters are for one MAME game: select a game in the MAME games list."));
        return;
    }
    customParameters(ParamsGame, false);
}

// PopupEditGameCustomParametersClick / PopupDeleteGameCustomParametersClick (and the parent's,
// the software list's and the machine's)
void MainWindow::customParameters(ParamsFor what, bool remove) {
    const MameGame* g = current();
    if (!g) return;
    const QString file = customParamsFile(*g, what);
    if (file.isEmpty()) return;
    if (remove) {
        QFileInfo fi(file);
        if (!fi.exists()) return;
        QString size = fi.size() < 1024 ? tr("%1 Bytes").arg(fi.size()) : QLocale().formattedDataSize(fi.size());
        if (QMessageBox::question(this, tr("Delete File"),
                                  tr("Delete custom parameters file.\n\nAre you sure you want to delete %1?\n\n"
                                     "File Size: %2\nDate/Time: %3")
                                      .arg(QDir::toNativeSeparators(file), size,
                                           QLocale().toString(fi.lastModified(), QLocale::ShortFormat))) == QMessageBox::Yes)
            moveToRecycleBin({file}, this);
        return;
    }
    MameGame target = *g;
    if (what == ParamsParent) {
        const QString key = g->isSoftware() ? g->softwareList + '\\' + g->cloneOf : g->cloneOf;
        if (const MameGame* p = db_.find(key)) target = *p;
        else { target = MameGame{}; target.name = g->cloneOf; target.title = g->cloneOf; target.softwareList = g->softwareList; }
    }
    CustomParametersDialog::Input in;
    in.what = what;
    if (what == ParamsMachine) {
        const QString m = QFileInfo(file).completeBaseName();
        if (const MameGame* p = db_.find(m)) target = *p;
        else { target = MameGame{}; target.name = m; target.title = m; }
        in.software = *g;
    }
    in.game = target;
    in.parent = what == ParamsParent;
    in.gameIcon = model_->icon(target);
    in.emuIcon = fe_.mainIcon(sys_ == SystemId::HBMAME ? "emu_hbmame.ico" : "emu_mame.ico");
    in.file = file;
    if (g->isSoftware()) {
        in.machines = machinesFor(g->softwareList);
        in.machine = SoftwareLists::lastMachine(machinesUsedFile(), g->softwareList);
        if (what != ParamsMachine) {
            SoftwareLists::Requirements req;
            req.load(requirementFile());
            in.requirement = SoftwareLists::requirementArgs(req, softwareFolder(), g->softwareList, target.name).join(' ');
        }
        in.machineIcon = [this](const MameGame& m) { return model_->icon(m); };
    }
    CustomParametersDialog dlg(fe_, sys_, in, this);
    dlg.exec();
}

// ------------------------------------------------------------ play statistics
// arcade\played_games\mame.txt (GetGamesPlayedIniFile), shared with Delphi EmuLoader.

QString MainWindow::playedFile(SystemId s) const {
    return fe_.userFile("arcade/played_games/" + Frontend::fileBase(s) + ".txt");
}

// The games list's Played, Last Played and Playtime columns.
void MainWindow::loadPlayStats() {
    played_.load(playedFile(sys_));
    for (MameGame& g : db_.games()) {
        PlayStat s = played_.value(g.key());
        g.played = s.times;
        g.lastPlayed = s.lastPlayed;
        g.playtime = s.playtime;
    }
}

// ResetPlayGameInfo: what 0 = all, 1 = total playtime, 2 = last played, 3 = times played.
void MainWindow::resetPlayInfo(int what) {
    QStringList names;
    QList<int> rows;
    for (const QModelIndex& i : list_->selectionModel()->selectedRows()) {
        int r = filter_->mapToSource(i).row();
        rows << r;
        names << db_.games()[r].name;
    }
    if (names.isEmpty()) return;
    played_.reset(playedFile(sys_), names, what);
    for (int r : rows) {
        MameGame& g = db_.games()[r];
        if (what == 0 || what == 1) g.playtime = 0;
        if (what == 0 || what == 2) g.lastPlayed = 0;
        if (what == 0 || what == 3) g.played = 0;
        model_->refreshRow(r);
    }
    updateStatus();
}

// FindGameName + SelectEasyItem: the game in the games list as it's filtered now.
QString MainWindow::startupGame() const {
    const QString soft = fe_.ini().readString(kStartupSection, "SoftwareName");
    const QString name = fe_.ini().readString(kStartupSection, "GameName");
    return soft.isEmpty() ? name : soft + '\\' + name;
}

bool MainWindow::selectGameByName(const QString& name) {
    for (int r = 0; r < filter_->rowCount(); ++r) {
        QModelIndex i = filter_->index(r, 0);
        if (model_->game(filter_->mapToSource(i).row()).key().compare(name, Qt::CaseInsensitive) == 0) {
            list_->selectionModel()->setCurrentIndex(i, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            gamesView()->scrollTo(i, QAbstractItemView::PositionAtCenter);
            return true;
        }
    }
    return false;
}

// Tool bar > Last Played Games (ButtonGamesMRUClick)
void MainWindow::showLastPlayed() {
    QList<LastPlayedDialog::System> systems;
    const bool hidePlayed = model_->options().hideSecondsPlayed, hidePlaytime = model_->options().hideSecondsPlaytime;
    const QString icons = fe_.resourcesFolder() + "main_icons/";
    for (SystemId id : {SystemId::MAME, SystemId::HBMAME}) {
        PlayedGames pg;
        if (!pg.load(playedFile(id)) || pg.order().isEmpty()) continue;
        Favorites fav;
        fav.load(fe_.userFile("favorites/" +
                                              fe_.ini().readString("Preferences", "FavoriteFile", "favorites.txt")),
                 Frontend::fileBase(id));
        LastPlayedDialog::System s;
        s.id = id;
        s.title = id == SystemId::MAME ? "MAME" : "HBMAME";
        s.icon = QIcon(fe_.mainIcon(id == SystemId::MAME ? "emu_mame.ico" : "emu_hbmame.ico"));
        s.star = QIcon(fe_.mainIcon("favorite_game.ico"));
        QHash<QString, const MameGame*> byName; // FillGameDataFromMainGamesList: titles of this system's list
        if (id == sys_)
            for (const MameGame& g : db_.games()) byName.insert(g.key().toLower(), &g);
        for (const QString& n : pg.order().mid(0, LastPlayedDialog::kTop)) {
            PlayStat st = pg.value(n);
            LastPlayedDialog::Game g;
            g.name = n;
            g.title = n.section('\\', -1);
            g.lastPlayed = PlayedGames::dateText(st.lastPlayed, hidePlayed);
            g.playtime = PlayedGames::playtimeText(st.playtime, hidePlaytime);
            g.played = st.times;
            g.favorite = fav.contains(n);
            if (const MameGame* m = byName.value(n.toLower())) {
                g.title = m->title;
                g.icon = model_->icon(*m);
            }
            s.games << g;
        }
        systems << s;
    }
    if (systems.isEmpty()) {
        QMessageBox::information(this, tr("Last Played Games"), tr("There are no games to show on any available system."));
        return;
    }
    LastPlayedDialog dlg(systems, sys_, [this](SystemId id, const QString& name) {
        return id == sys_ && selectGameByName(name);
    }, this);
    dlg.exec();
    gamesView()->setFocus(); // FocusGamesList
}

// ------------------------------------------------------------ favorites
// The original's favorites file (favorites\favorites.txt, or the profile in
// [Preferences] FavoriteFile), shared with Delphi EmuLoader.

void MainWindow::loadFavorites() {
    WinIni& ini = fe_.ini();
    QString file = fe_.userFile("favorites/" + ini.readString("Preferences", "FavoriteFile", "favorites.txt"));
    favorites_.load(file, Frontend::fileBase(sys_));
    // Early builds of this port kept favorites in [EmuLoaderQt] (now [EmuLoaderRL]) Favorites: move them to the file.
    QString old = ini.readString(kSection, "Favorites");
    if (!old.isEmpty()) {
        bool added = false;
        for (const QString& f : old.split(',', Qt::SkipEmptyParts)) added |= favorites_.add(f);
        if (added) favorites_.save();
    }
    ini.deleteKey(kSection, "Favorites");
    model_->favorites() = favorites_.names();
}

// Favorites button's menu > More... (PopupFavoritesManagerClick)
void MainWindow::openFavoritesManager() {
    FavoritesManagerDialog::Setup st;
    st.folder = fe_.userFile("favorites");
    QDir().mkpath(st.folder);
    st.activeFile = fe_.ini().readString("Preferences", "FavoriteFile", "favorites.txt");
    st.extrasIni = extrasIni();
    st.selectOnly = filter_->favoritesOnly();
    st.activeIcon = QIcon(fe_.mainIcon("favorite_game.ico"));
    st.settingsIcon = QIcon(fe_.mainIcon("settings.ico"));
    // Cleanse: MAME and HBMAME entries are checked against their games lists; other systems and
    // software list entries are kept (this version can't tell whether they're still valid).
    QHash<QString, std::shared_ptr<ElDatabase>> lists;
    st.isValid = [this, lists](const QString& line) mutable {
        int eq = line.lastIndexOf('=');
        if (eq <= 0) return true;
        QString name = line.left(eq), sys = line.mid(eq + 1).toLower();
        if (sys != "mame" && sys != "hbmame") return true;
        SystemId id = sys == "mame" ? SystemId::MAME : SystemId::HBMAME;
        // software list entries ("nes\smb"): checked when this system's software list games are loaded
        if (name.contains('\\')) return id != sys_ || db_.softwareCount() == 0 || db_.find(name) != nullptr;
        if (id == sys_) return db_.find(name) != nullptr;
        auto& other = lists[sys];
        if (!other) {
            other = std::make_shared<ElDatabase>();
            if (!QFileInfo::exists(fe_.databaseFile(id))) return true; // no list for that system here
            other->load(fe_.databaseFile(id), fe_.statusFile(id));
        }
        return other->games().isEmpty() || other->find(name) != nullptr;
    };
    st.systemOf = [](const QString& line) {
        QString sys = line.mid(line.lastIndexOf('=') + 1);
        return sys.compare("mame", Qt::CaseInsensitive) == 0 ? QStringLiteral("MAME")
             : sys.compare("hbmame", Qt::CaseInsensitive) == 0 ? QStringLiteral("HBMAME") : sys;
    };
    FavoritesManagerDialog dlg(st, this);
    dlg.exec();
    bool switched = dlg.activeFile().compare(st.activeFile, Qt::CaseInsensitive) != 0;
    if (switched) fe_.ini().writeString("Preferences", "FavoriteFile", dlg.activeFile());
    if (switched || dlg.activeChanged()) {
        rememberSelection();
        loadFavorites();
        model_->reload();
        if (filter_->favoritesOnly()) afterFilterChange();
        else applyColumns();
    }
    gamesView()->setFocus(); // FocusGamesList
}

// Delete / copy / move the selected games' files (PopupDeleteSelectedGamesClick)
void MainWindow::gameFiles(FileAction action) {
    QList<const MameGame*> games;
    for (const QModelIndex& i : list_->selectionModel()->selectedRows())
        games << &model_->game(filter_->mapToSource(i).row());
    if (games.isEmpty()) return;
    // Protect Game Files (DisableDeleteSelectedGames): a message instead of grayed-out menu items
    if (fe_.ini().readBool("Preferences", "DisableDeleteSelectedGames", false)) {
        DiagnosticLog::write("files", "Protect Game Files is on: nothing deleted, copied or moved");
        QMessageBox::information(this, tr("Game Files Are Protected"),
                                 tr("Deleting, copying and moving game files is turned off, so nothing was changed.") + "\n\n" +
                                     tr("To allow it again, turn off Protect Game Files in Menu Options > Settings, "
                                        "on the General tab."));
        return;
    }
    QString caption = action == FileAction::Delete ? tr("Delete Selected Games Files")
                    : action == FileAction::Copy ? tr("Copy Selected Games Files to Folder")
                                                 : tr("Move Selected Games Files to Folder");
    const QStringList roms = fe_.romFolders(sys_);
    if (roms.isEmpty() && action != FileAction::Delete) {
        QMessageBox::information(this, caption, tr("There is nothing to do: no ROM folders are set for MAME."));
        return;
    }
    // LoadFolders_ConfigFiles: MAME's cfg and nvram folders
    MameIniFile mi;
    mi.load(fe_.mameIniPath(sys_));
    auto folder = [&](const char* key, const char* def) {
        QStringList l = splitPathList(mi.get(key, def));
        return l.isEmpty() || fe_.mameFolder(sys_).isEmpty() ? QString() : resolvePath(fe_.mameFolder(sys_), l.first());
    };
    const QString cfgDir = folder("cfg_directory", "cfg"), nvramDir = folder("nvram_directory", "nvram");
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QList<GameFilesDialog::Game> list;
    for (const MameGame* g : games) {
        if (g->isSoftware()) {
            // roms\<list>\<name>.zip and roms\<list>\<name>\ (CHDs); its settings are its machine's
            MameGame soft = *g;
            soft.name = g->imageName();
            soft.cloneOf = g->imageParent();
            list.append({g->key(), g->title, findGameFiles(soft, roms, {}, {})});
            continue;
        }
        list.append({g->name, g->title, findGameFiles(*g, roms, cfgDir, nvramDir)});
    }
    QApplication::restoreOverrideCursor();
    GameFilesDialog dlg(action, caption, list, extrasIni(), Frontend::section(sys_), this);
    dlg.exec();
    if (dlg.changedFiles()) rescanRoms(); // the sets that are gone show as missing
    gamesView()->setFocus();
}

QList<const MameGame*> MainWindow::selectedGames() const {
    QList<const MameGame*> games;
    for (const QModelIndex& i : list_->selectionModel()->selectedRows())
        games << &model_->game(filter_->mapToSource(i).row());
    if (games.isEmpty())
        if (const MameGame* g = current()) games << g;
    return games;
}

bool MainWindow::changeList(Favorites& list, bool add, const QString& caption, const QString& notRemoved) {
    QList<const MameGame*> games = selectedGames();
    if (games.isEmpty()) return false;
    bool changed = false;
    for (const MameGame* g : games) changed |= add ? list.add(g->key()) : list.remove(g->key());
    if (!changed) {
        const QString file = QDir::toNativeSeparators(list.file());
        QMessageBox::information(this, caption,
                                 add ? tr("The selected games are already in the list, so none were added.\n\nFile: %1").arg(file)
                                     : QString(notRemoved).arg(file));
        return false;
    }
    if (!list.save()) {
        FileProblem::cantSave(this, caption, list.file());
        return false;
    }
    return true;
}

void MainWindow::changeFavorite(bool add) {
    if (!changeList(favorites_, add, add ? tr("Add Games To Favorites") : tr("Remove Games From Favorites"),
                    tr("No games were removed from the favorites list.\n\nFile: %1")))
        return;
    rememberSelection();
    model_->favorites() = favorites_.names();
    model_->reload();
    applyColumns(); // a model reset clears the header layout
    afterFilterChange();
}

// ------------------------------------------------------------ Special List
// A second list of games, like favorites but with no profiles (ini_files\special.txt, "pacman=mame"
// lines, shared with the original). Misc > Special List shows only these games, or hides them.

void MainWindow::loadSpecial() {
    special_.load(fe_.iniFilesFolder() + "special.txt", Frontend::fileBase(sys_));
    model_->special() = special_.names();
}

void MainWindow::changeSpecial(bool add) {
    if (!changeList(special_, add, add ? tr("Add Games To Special List") : tr("Remove Games From Special List"),
                    tr("No games were removed from the special list.\n\nFile: %1")))
        return;
    rememberSelection();
    model_->special() = special_.names();
    model_->reload();
    applyColumns();
    afterFilterChange();
}

// ------------------------------------------------------------ persistence

// Window, splitter and filters use EmuLoader's own keys, so both programs share them.
void MainWindow::restoreState() {
    WinIni& ini = fe_.ini();
    int w = ini.readInteger(kSection, "Width", width()), h = ini.readInteger(kSection, "Height", height());
    resize(w, h);
    filter_->setHaveMiss(ini.readInteger("ListFilter", "HaveMiss", GamesFilter::ListAll));
    filter_->setParentClone(ini.readInteger("ListFilter", "ParentClone", GamesFilter::Both));
    filter_->setDriver(ini.readInteger("ListFilter", "DriverStatus", GamesFilter::AllStatus));
    filter_->setDriverCategory(std::clamp(ini.readInteger("ListFilter", "DriverStatusCategory", 0), 0, 4));
    // [ListFilter] ArcadeGames is the original's selected filter; "favorites" = the favorites list.
    { // the favorites list takes the place of the games filter, which is kept in ArcadeGames_LastFilter
        QString sel = ini.readString("ListFilter", "ArcadeGames", "allgames");
        bool fav = sel.compare("favorites", Qt::CaseInsensitive) == 0;
        filter_->setFavoritesOnly(fav);
        filter_->setArcadeFilter(fav ? ini.readString("ListFilter", "ArcadeGames_LastFilter", "allgames") : sel);
    }
    loadGameFilters();
    if (QAction* a = menus_.action("PopupEnableFavorites")) a->setChecked(filter_->favoritesOnly());
    filter_->setMachineTypes(MachineFilterDialog::load(fe_.gamesFiltersFile()));
    filter_->setConsoleComputer(PreferencesDialog::consoleComputerSupport(fe_));
    setSearchField(ini.readInteger("ListFilter", "SearchBarFilterIndex", GamesFilter::SearchTitle));
    if (QAction* a = menus_.action("FilterTextBarIncludeToolBarFilters")) {
        a->setCheckable(true);
        a->setChecked(ini.readBool("ListFilter", "SearchBarFilterIncludeFilters", true));
    }
    loadFavorites();
    loadSpecial();
    int imgW = ini.readInteger("Configuration", "MainSplitterPos", -1); // = PanelScreenshotsArea.Width
    if (imgW > 50) vcl::place(ui_->PanelScreenshotsArea, QRect(ui_->PanelScreenshotsArea->pos(), QSize(imgW, ui_->PanelScreenshotsArea->height())));
    vcl::relayout(this);
    updateFilterIcons();
    loadLayoutSettings();
    setImageLayout(ini.readInteger("Images", "LayoutIndex", 0), false);
}

void MainWindow::saveState() {
    WinIni& ini = fe_.ini();
    ini.writeInteger("Images", "Stretch", imageScale_.stretch ? 1 : 0);
    ini.writeInteger("Images", "StretchLargerImages", imageScale_.largerOnly ? 1 : 0);
    ini.writeInteger("Images", "DisableAspectRatio", imageScale_.ignoreAspect ? 1 : 0);
    ini.writeInteger("Images", "DisplayParent", displayParent_ ? 1 : 0);
    ini.writeInteger("Images", "ShowImages", showImages_ ? 1 : 0);
    ini.writeInteger("Images", "DisplayGameSnapWhenNoArtwork", snapWhenNoArtwork_ ? 1 : 0);
    ini.writeInteger("ToolBar", "ImagesBar_Visible", ui_->PanelToolBarImages->isVisible() ? 1 : 0);
    ini.writeInteger("Images", "LayoutIndex", layouts_->layout());
    saveDocsSettings();
    {
        WinIni lay;
        lay.load(fe_.iniFilesFolder() + "screenshot_layouts.ini");
        lay.writeInteger("Single", "image1_category", imageCategory_);
        lay.writeInteger("Single", "image1_video", imageVideo_ ? 1 : 0);
        layouts_->writeSizes(lay);
        lay.save(fe_.iniFilesFolder() + "screenshot_layouts.ini");
        WinIni cat;
        cat.load(fe_.iniFilesFolder() + "image_category.ini");
        if (singleBack_ || cat.sectionExists("SingleBackground")) {
            cat.writeInteger("SingleBackground", "Enabled", singleBack_ ? 1 : 0);
            cat.writeInteger("SingleBackground", "Color", singleBackColor_);
            cat.save(fe_.iniFilesFolder() + "image_category.ini");
        }
    }
    if (!isMaximized()) {
        ini.writeInteger(kSection, "Width", width());
        ini.writeInteger(kSection, "Height", height());
    }
    ini.writeString("Configuration", "WindowState", isMaximized() ? "Maximized" : "Normal");
    ini.writeInteger("Configuration", "MainSplitterPos", ui_->PanelScreenshotsArea->width());
    ini.writeInteger("ListFilter", "HaveMiss", filter_->haveMiss());
    ini.writeInteger("ListFilter", "ParentClone", filter_->parentClone());
    ini.writeInteger("ListFilter", "DriverStatus", filter_->driver());
    ini.writeInteger("ListFilter", "DriverStatusCategory", filter_->driverCategory());
    // ToggleFavoriteLastFilter: the games filter in use under the favorites goes to ArcadeGames_LastFilter
    if (filter_->favoritesOnly()) {
        ini.writeString("ListFilter", "ArcadeGames", "favorites");
        ini.writeString("ListFilter", "ArcadeGames_LastFilter", filter_->arcadeFilter());
    } else {
        ini.writeString("ListFilter", "ArcadeGames", filter_->arcadeFilter());
        ini.writeString("ListFilter", "ArcadeGames_LastFilter", "");
    }
    ini.writeInteger("ListFilter", "SearchBarFilterIndex", searchField_);
    if (QAction* a = menus_.action("FilterTextBarIncludeToolBarFilters"))
        ini.writeInteger("ListFilter", "SearchBarFilterIncludeFilters", a->isChecked() ? 1 : 0);
    if (const MameGame* g = current()) ini.writeString("Selected", "LastSelected", g->key());
    saveColumns();
    fe_.saveIni();
}

// ini_files\columns_profile.ini: [Width] [Visible] [Position] by column name, [Sort] ColumnIndex/Direction
// (TFormMain.LoadColumnsProfile / SaveColumnsProfile).
QString MainWindow::columnsFile() const { return fe_.iniFilesFolder() + "columns_profile.ini"; }

void MainWindow::applyColumns() {
    QHeaderView* h = list_->header();
    WinIni ini;
    bool have = ini.load(columnsFile());
    for (int c = 0; c < GamesModel::ColumnCount; ++c) {
        h->resizeSection(c, have ? ini.readInteger("Width", GamesModel::columnKey(c), GamesModel::defaultWidth(c))
                                  : GamesModel::defaultWidth(c));
        if (c > 0) h->setSectionHidden(c, have ? !ini.readBool("Visible", GamesModel::columnKey(c), true) : false);
    }
    if (have) {
        QVector<int> byPos(GamesModel::ColumnCount, -1);
        bool ok = true;
        for (int c = 1; c < GamesModel::ColumnCount; ++c) {
            int p = ini.readInteger("Position", GamesModel::columnKey(c), c);
            if (p < 1 || p >= GamesModel::ColumnCount || byPos[p] != -1) { ok = false; break; }
            byPos[p] = c;
        }
        if (ok)
            for (int p = 1; p < GamesModel::ColumnCount; ++p)
                if (byPos[p] >= 0) h->moveSection(h->visualIndex(byPos[p]), p);
        int sc = ini.readInteger("Sort", "ColumnIndex", 0);
        list_->sortByColumn(sc >= 0 && sc < GamesModel::ColumnCount ? sc : 0,
                            ini.readInteger("Sort", "Direction", 0) == 1 ? Qt::DescendingOrder : Qt::AscendingOrder);
    }
}

void MainWindow::saveColumns() {
    // Same as TFormMain.UpdateColumnProfile: only what differs from the defaults is written.
    QHeaderView* h = list_->header();
    WinIni ini;
    ini.load(columnsFile()); // keep anything else in the file
    // A hidden column reports width 0 and can't be resized, so its width stays as it was.
    int widths[GamesModel::ColumnCount];
    for (int c = 0; c < GamesModel::ColumnCount; ++c)
        widths[c] = h->isSectionHidden(c) ? ini.readInteger("Width", GamesModel::columnKey(c), GamesModel::defaultWidth(c))
                                          : h->sectionSize(c);
    for (const char* s : {"Position", "Width", "Visible", "Sort"}) ini.eraseSection(s);
    for (int c = 0; c < GamesModel::ColumnCount; ++c) {
        const char* key = GamesModel::columnKey(c);
        if (c > 0 && h->visualIndex(c) != c) ini.writeInteger("Position", key, h->visualIndex(c));
        if (widths[c] != GamesModel::defaultWidth(c)) ini.writeInteger("Width", key, widths[c]);
        if (h->isSectionHidden(c)) ini.writeInteger("Visible", key, 0);
    }
    int sc = h->sortIndicatorSection();
    bool desc = h->sortIndicatorOrder() == Qt::DescendingOrder;
    if (sc < 0 || sc >= GamesModel::ColumnCount) sc = 0;
    if (sc != 0 || desc) {
        ini.writeInteger("Sort", "ColumnIndex", sc);
        ini.writeInteger("Sort", "Direction", desc ? 1 : 0);
    }
    QDir().mkpath(fe_.iniFilesFolder());
    ini.save(columnsFile());
}

void MainWindow::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (list_) gamesView()->setFocus();
    if (!layoutShown_) {
        // The layout's default sizes come from the panel's size, final only once the window is shown.
        layoutShown_ = true;
        QTimer::singleShot(0, this, [this] { setImageLayout(layouts_->layout(), false); });
    }
}

void MainWindow::changeEvent(QEvent* e) {
    QWidget::changeEvent(e);
    // The selection bar has active / inactive colors: redraw it when the window is (de)activated.
    if (e->type() == QEvent::ActivationChange && list_) list_->viewport()->update();
    // Restored or maximized: the whole window afresh, once the new size has been applied.
    if (e->type() == QEvent::WindowStateChange && list_ && !(windowState() & Qt::WindowMinimized))
        QTimer::singleShot(0, this, [this] { fullRepaint(); });
}

void MainWindow::fullRepaint() {
    if (!isVisible() || isMinimized()) return;
#ifdef Q_OS_WIN
    RedrawWindow(reinterpret_cast<HWND>(winId()), nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
#endif
    update(); // Qt's own drawing of every part of the window, the list included
    if (list_) list_->viewport()->update();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    DiagnosticLog::write("lifecycle", "Main window close requested; saving settings");
    stopVideo();
    playDummyVideo(true); // stops the video that's playing
    saveState();
    e->accept();
}

} // namespace eld
