// MainWindow - port of EmuLoader's TFormMain (uMain.pas) for the MAME family.
#pragma once

#include "ElDatabase.h"
#include "RomAudit.h"
#include "Frontend.h"
#include "GamesModel.h"
#include "GameFonts.h"
#include "Favorites.h"
#include "PlayedGames.h"
#include "GameFiles.h"
#include "GameDocs.h"
#include "ImageFinder.h"
#include "VideoPreview.h"
#include "GameFilters.h"
#include "CustomParametersDialog.h"
#include "GamesFiltersDialog.h"
#include "NightMode.h"
#include "MamuIcons.h"
#include <QAbstractItemView>
#include <QElapsedTimer>
#include <array>
#include <functional>
#include <memory>

#include "ImageLayouts.h"
#include "ImageView.h"
#include "VideoPanel.h"
#include "LightMode.h"
#include "Menus.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QPixmap>
#include <QPointer>
#include <atomic>
#include <memory>

namespace Ui { class FormMain; }
class QProcess;
class QTimer;
class QTreeView;

namespace eld {
class SettingsDialog;

// Shows or hides MAME's console window (Preferences > Hide Command Prompt Box).
void commandPromptBox(QProcess* p, bool hide, bool readOutput = false);

class ElButton;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    // A game MAME (or HBMAME) couldn't run: what it said (MainWindowGameFailed.cpp)
    struct GameAttempt {
        QString emulator; // "MAME", "HBMAME"
        int code = 0;
        bool crashed = false;
        QString message;  // its error output
    };
    explicit MainWindow(Frontend& fe, QWidget* parent = nullptr);
    ~MainWindow() override;
    bool startMaximized() const;
    // Is there a newer EmuLoader ReLoaded? (MainWindowUpdates.cpp) manual: Help > Check for
    // Updates, which says what it found; else the weekly check at start, which only speaks up
    // about a newer version (not one the user skipped). autoCheckForUpdates: when a week has gone
    // by since the last one and the check is on (Settings > General).
    void checkForUpdates(bool manual);
    void autoCheckForUpdates();

protected:
    void closeEvent(QCloseEvent*) override;
    void showEvent(QShowEvent*) override;
    void changeEvent(QEvent*) override;
    // The whole window drawn afresh, by Windows itself (RedrawWindow): no old picture of it is
    // kept after MAME changed the screen resolution, was killed or hung.
    void fullRepaint();
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void setupToolBar();
    void setupMenus();
    void setupList();
    void loadGamesList();
    void createGamesList();
    void firstStartImported();                    // after the first start's import from the original
    void rescanRoms();
    // MAME software list games (MainWindowSoftware.cpp)
    QString softwareFolder() const;               // arcade\mame_softwarelist_games\ (in this version's folder)
    QString requirementFile() const;              // arcade\games\mame.elsoftlistreq
    QString machinesUsedFile() const;             // arcade\mame_machines_usercustom.ini
    QString softwareExcludeFile() const;          // arcade\mame_softlist_exclude.txt
    static QStringList softwareListNames(const ElDatabase& db); // the lists its machines run
    QVector<MameGame> machinesFor(const QString& list) const;
    bool makeSoftwareLists(class ProgressWindow& win, int mode, const ElDatabase& machines);
    void scanSoftware(QVector<MameGame>& games);
    void createSoftwareListGames();
    QString selectMachine(const MameGame& g, bool showSingle);
    QString softwareCustomFile(const MameGame& g, const QString& machine) const;
    QStringList softwareArgs(const MameGame& g);
    bool multiSlotArgs(const MameGame& g, QStringList* args); // several Neo-Geo (ST-V ...) games in one machine
    // the Machines List panel (MainWindowMachines.cpp)
    QString machinesExcludeFile() const;          // arcade\mame_machines_exclude.txt
    void setupMachinesPanel();
    void showMachinesPanel();                     // shown or hidden, as the settings say
    void fillMachinesPanel();
    int findMachineRow(const QString& name) const;
    void selectMachineRow(const QString& name);
    void useMachine(const QString& name);         // its software list games listed
    void updateMachinesStatus();
    void excludeMachine(bool add);
    void customizeMachines();
    void customizeSoftwareLists();
    class QStandardItemModel* machinesModel_ = nullptr;
    QString panelMachine_;                        // the machine whose games are listed
    void openEmulatorSettings(const QString& reveal = {});
    void openCustomSettings(int level, bool remove, const std::function<void(MameGame&)>& adjust = {});
    void runGame(const QStringList& extra = {});
    void deleteRenameImage(bool rename);          // image menu > Rename / Delete Image
    void deleteCloneImages();
    void openImagesManager(int category = -1);    // Menu Options > Images > MAME Images Manager (MAMu_ Icons Manager: Icons)
    void setupImageFinder();
    // Video previews (VideoPreview*): the selected game's video, played with the media player
    void loadVideoPreview();                      // videopreview.ini, buttons shown or hidden (SetVideoPreviewState)
    void searchHelp();
    void applyHeaderFont();
    void customizeHeaderFont(bool toDefault);
    void layoutSearchPanel();                     // FilterSearchBarAdjustPanelControls
    void findManual();                            // the selected game's PDF manual: the button enabled
    void openManual();
    void openInternetGameInfo();
    QString internetGameInfoUrl(const MameGame& g); // the page's address (the local page written)
    QString manualFile_;
    void findVideoPreview(bool autoPlay);         // the selected game's video; buttons enabled; AutoPlay
    void playVideoPreview(bool dummy = false, bool paused = false); // VideoPreviewPlay: the video, or novideo.avi to stop it
    void playDummyVideo(bool force);              // VideoPreviewPlayDummy
    // EmuLoader's own player (MainWindowVideo.cpp): the video in the snapshot's picture panel
    bool internalVideo() const;                   // the built-in player plays (chosen, and its library is there)
    bool canPlayVideo() const;                    // a player for the videos: the built-in one or another
    void toggleVideoPreview();                    // Shift+V, the Play Video Preview button: play, or stop
    void stopVideo();
    ImageView* videoView();                       // the panel the video plays in (Game Snapshot, else the first)
    void showVideoMenu(const QPoint& global);
    void openVideoElsewhere();                    // the media player of Advanced, else the system's player
    // The Video category: a picture panel that plays the game's video over its snapshot
    bool videoInPanels() const;                   // video previews on, with EmuLoader's own player
    bool slotVideo(int slot) const;               // that panel shows Video
    ImageView* videoSlotView() const;             // the first panel shown that shows Video, or none
    void videoPanelsChanged();                    // a category or layout change: play, or stop
    void showNoVideo(ImageView* v);               // a Video panel, for a game without a video
    void showVideoWaiting(ImageView* v);          // a Video panel, for a game with one: blank, not the snapshot
    void cueVideo();                              // the video in the Video panel, paused on its first frame
    void showLayoutDimensions(bool on);           // image menu > Show Layout Dimensions
    void updateLayoutDimensions();
    void runGameExtra();                          // Run Game (Extra)
    // Custom Parameters > Set/Delete Game / Parent Game / Software List / MAME Machine Parameters
    using ParamsFor = CustomParametersDialog::For;
    static constexpr ParamsFor ParamsGame = CustomParametersDialog::Game, ParamsParent = CustomParametersDialog::Parent,
                               ParamsSoftwareList = CustomParametersDialog::SoftwareList, ParamsMachine = CustomParametersDialog::Machine;
    QString customParamsFile(const MameGame& g, ParamsFor what) const; // "" when it doesn't apply
    void customParameters(ParamsFor what, bool remove);
    void prepareCustomParamsMenu(const MameGame& g);
    void updateStatus();
    void updateImage();
    void updateFilterIcons();
    void saveState();
    void restoreState();
    const MameGame* current() const;
    QIcon toolIcon(const QString& file, bool filters = true) const;
    QIcon toolbarIcon(const QString& file, bool filters, int overlay) const;
    QSize toolBarPictureSquare() const;
    void setToolBarIcon(ElButton* b, const QString& file, bool filters, int overlay);
    void layoutToolBar();
    void placeToolBarButtons();
    void showToolBarOverflow();
    QList<QPair<QString, QString>> controlTypes() const;
    void showControlsFilterMenu();
    void filterByControl(const QString& control); // the search bar's Controls filter
    QList<class ElButton*> toolBarButtons() const; // the original's buttons, in its order (not Guide)
    // The tool bar's order ([EmuLoaderRL] ToolBarOrder, MainWindowToolBar.cpp): button names,
    // "|" for a separator, "space" for a space that pushes what follows to the right end.
    QStringList toolOrder_;
    QList<QWidget*> toolSeparators_;
    QStringList toolOriginalOrder() const; // the original's buttons, all shown (8.9.4)
    QStringList toolDefaultOrder() const;  // the same, then a space and the Guide button
    void loadToolOrder();
    void setToolOrder(const QStringList& order, bool save = true);
    void setupGuideButton();
    // ran: the game ran, and MAME wrote something to its error output (artwork it couldn't load ...)
    void showGameFailed(const QString& title, const QString& name, const GameAttempt& attempt, bool ran = false);
    // A game that ran, with what MAME said: a line in the status bar that shows it when clicked
    void noteGameWarnings(const QString& title, const QString& name, const GameAttempt& attempt);
    void startupSetupCheck();               // no emulator, its file gone, or no games list (MainWindowSetupCheck.cpp)
    void askCreateGamesList(bool afterSetup);
    void openEmulatorSetup(); // Settings > Emulators, its file field flashing
    // The games list showing no games: why, and a button (MainWindowEmptyList.cpp)
    void updateEmptyListMessage();
    QStringList filtersInUse() const;
    void clearAllFilters();
    QWidget* emptyList_ = nullptr;
    // A start-up question still to come or showing (first start, no emulator, no games list):
    // the empty list's message waits for its answer (it would say the same thing behind it)
    bool setupPending_ = true;
    int setupAsking_ = 0;
    struct SetupAsking {
        MainWindow* w;
        explicit SetupAsking(MainWindow* m) : w(m) { ++w->setupAsking_; w->updateEmptyListMessage(); }
        ~SetupAsking() { --w->setupAsking_; w->updateEmptyListMessage(); }
    };
    void showGameNotStarted(const QString& emulator, const QString& exe, const QString& reason);
    // .7z picture and icon archives (MainWindowSevenZip.cpp)
    QStringList sevenZipArchives() const;
    void offerSevenZipConversion(bool atStart);
    bool sevenZipAsked_ = false;
    void setupUpdateCheck();
    bool updateChecking_ = false;
    // The main CPU filter's row under the tool bar (MainWindowToolBar.cpp)
    void setupCpuFilter();
    void showCpuFilter(bool on);
    void fillCpuList();
    void applyCpuFilter(const QString& cpu, bool exact);
    friend class GuideKey;
public:
    static QString guideAnchor(QWidget* window); // F1: the guide's section for a window
private:
    void setToolBarIconSize(int idx);
    ElButton* overflow_ = nullptr;
    // The MAME Problems tool bar button (MainWindowGameFailed.cpp): what MAME reported about the
    // game just played
    std::function<void()> problemsShow_;
    void setupProblemsButton();
    void problemsIcon(bool flashGray = false);
    void hideGameNotice();
    int toolButtonHeight_ = 0;
    void openFontSettings();
    void openMachineFilter();
    void openMiscFilters();                       // Misc button (ButtonExtraFiltersClick)
    void openGamesFilters();                      // "All Games" button (ButtonArcadeGamesFiltersClick)
    void loadGameFilters();                       // Miscellaneous Filters and their data files
    QString arcadeFilterTitle(const QString& name) const; // "Capcom -> Capcom System 1 (CPS-1)"
    void applyConsoleComputer();
    void applyPreferences();
    void applyLightMode(const LightMode& lm);
    // image panel
    void setupImagePanel();
    void updateCategoryButton();
    void refreshImageFolders();
    void loadFavorites();
    void openFavoritesManager();
    void gameFiles(FileAction action);
    void changeFavorite(bool add);
    void loadSpecial();
    void changeSpecial(bool add);
    // Add to / remove from favorites or the Special List: every selected game (AddToSpecialList)
    bool changeList(Favorites& list, bool add, const QString& caption, const QString& notRemoved);
    QList<const MameGame*> selectedGames() const;
    Favorites favorites_;
    Favorites special_;
    void incrementalSearch(const QString& typed);
    QPointer<QWidget> splitterForMenu_; // the splitter whose menu is open
    bool confirmRun(const MameGame& g, const QString& exe, const QStringList& args);
    void applyToolBarPlacement();
    void setScreenshotsWidth(int w);
    QString incBuffer_;
    QElapsedTimer incTimer_;
    bool startedUp_ = false; // the games list was shown once (Select Game at Startup)
    static constexpr const char* kStartupSection = "SelectGameStartup"; // the Special List: ini_files\special.txt, the same format as favorites
    // Play statistics (arcade\played_games\mame.txt, shared with Delphi EmuLoader)
    PlayedGames played_;
    QString playedFile(SystemId s) const;
    void loadPlayStats();
    void resetPlayInfo(int what);
    void showLastPlayed();
    bool selectGameByName(const QString& key);     // a machine's name, or "<list>\\<name>"
    // Scan Games (Audit ROMs)..., View Missing ROMs/CHDs (MainWindowAudit.cpp)
    void openScanGames();
    void scanGames(int which);                    // 0 all, 1 available, 2 missing, 3 missing ROMs/CHDs, 4 selected
    void viewScanResults(bool selectedOnly, const RomAudit::Results* scan = nullptr);
    QVector<int> scanRows(int which);
    int scanSetsMode();
    void scanDone();
    bool runVerify(const QStringList& args, QString& output, ProgressWindow& win, int done, const QString& count);
    bool runAudit(const QVector<int>& rows, bool samples, RomAudit::Results& out, ProgressWindow& win, bool& canceled);
    QString auditFile() const;
    void checkChds(RomAudit::Results& results);
    void openGuide(const QString& anchor = {});
    QString startupGame() const;                  // Select Game at Startup's game (its key)
    void setImageCategory(int c);
    void cycleCategory(bool forward);
    void cycleSlotCategory(int slot, bool forward); // the mouse wheel over a picture
    int nextCategory(int now, bool video, bool forward) const;
    void selectImageCategory();
    int pickCategory(int current, QWidget* under);
    void stepImage(int step, int onlySlot = 0);
    ImageView* newImageView(QWidget* parent);
    int slotOf(const ImageView* v) const;
    int slotCategory(int slot) const;
    void forEachView(const std::function<void(ImageView*)>& f);
    QString notFoundReason(int category, const ImageFinder::Found& f, bool* fix) const;
    void loadImage(ImageView* v, int category, int& index, const MameGame* g);
    QPoint popupPos(QWidget* button, const QSize& size) const;
    // image layouts
    void loadLayoutSettings();
    void setImageLayout(int layout, bool saveOld);
    void cycleLayout(bool forward);
    void selectImageLayout();
    // Settings (MainWindowSettings.cpp): every settings window as a tab of one window; tab is
    // its title ("Video"), reveal a control to show (Find a Setting)
    void openSettings(const QString& tab = {}, const QString& reveal = {});
    using AfterSettings = QVector<std::function<void()>>;
    void addToolBarPage(SettingsDialog& s, AfterSettings& after);
    void addNightModePage(SettingsDialog& s, AfterSettings& after);
    void addThumbnailsPage(SettingsDialog& s, AfterSettings& after);
    void addVideoPage(SettingsDialog& s, AfterSettings& after);
    void addPicturesPage(SettingsDialog& s, AfterSettings& after);
    void addLayoutsPage(SettingsDialog& s, AfterSettings& after);
    void addEmulatorsPage(SettingsDialog& s, AfterSettings& after);
    // Find a Setting
    struct MenuPlace { QString name, title; QPoint at; };
    QList<MenuPlace> menuPlaces() const;
    void findSetting(const QString& query = {}, int row = -1);
    void styleSplitters();
    void layoutImageToolBar();
    // game documents (MainWindowDocs.cpp)
    void setupGameDocs();
    void readDocsSettings();
    void saveDocsSettings();
    void reloadDocs();
    void setDocsEnabled(bool on);
    void applyDocsLayout();
    void applyDocsLook();
    void updateDocs();
    QString extrasIni() const;
    GameDocs docs_;
    bool docsReady_ = false, docsOn_ = false, docsTabs_ = false;
    int docsTab_ = 0, docsLayout_ = 0;
    std::array<int, 4> docsSize_{151, 151, 151, 151};
    QList<QPair<int, bool>> docsOrder_;
    ImageView* imageView_ = nullptr;
    std::unique_ptr<ImageLayouts> layouts_;
    ImageLayouts::Settings layoutSettings_;
    bool layoutVisible_[ImageLayouts::Count] = {};
    bool layoutShown_ = false;
    bool useConsComp_ = false;
    std::array<int, 4> slotIndex_{1, 1, 1, 1};
    QPointer<ImageView> popupView_;               // the picture the image menu is for
    QHash<const ImageView*, std::shared_ptr<std::atomic<int>>> imageWanted_; // each panel's picture wanted now (its reads, in the background)
    QWidget* dimensions_ = nullptr;               // Show Layout Dimensions (FormImageLayoutDimensions)
    ImageView::Hint imageHint_;
    ImageFinder imageFinder_;
    ImageView::Scale imageScale_;
    int imageCategory_ = 1;
    bool imageVideo_ = false;                    // the single layout shows Video (imageCategory_ is the snapshot)
    bool videoCategoryOn_ = true;                // the Video category is ticked in Settings > Pictures
    QString imageGame_;
    bool displayParent_ = true, snapWhenNoArtwork_ = false, showImages_ = true, singleBack_ = false;
    int singleBackColor_ = 0;
    int categoryBack_[17] = {};
    bool categoryEnabled_[17] = {};
    LightMode lightMode_;
    class GamesListDelegate* listDelegate_ = nullptr;
    class MultilineDelegate* machinesDelegate_ = nullptr; // the Machines List panel's captions
    class GamesIconView* iconView_ = nullptr;     // Small/Large Icons, Tiles, Thumbnails
    int viewMode_ = 0;                            // ButtonViewMode.Tag: 0 Details ... 5 Thumbnails
    QPair<int, Qt::SortOrder> sortBeforeGroups_{0, Qt::AscendingOrder};
    void setupIconView();
    void setViewMode(int mode, bool save = true);
    QAbstractItemView* gamesView() const;         // the view that shows the games now
    void showGamesMenu(const QPoint& global);
    // Night Mode
    bool night_ = false;
    NightMode nightMode_;
    QString nightFile() const;
    QString gameFontsFile() const;
    LightMode loadLook();
    void saveLook(const std::function<void(LightMode&)>& change);
    void applyNight(bool on, bool save = true);
    void showNight(bool on, const LightMode& look);
    QPalette nightPalette(bool on) const;
    MamuIcons mamu_;
    void refreshIcons();                          // the games' icons again (MAMu_ icons changed)
    QPixmap listBackground_;
    void applySearch();
    void afterFilterChange();
    void setSearchField(int field);
    int searchField_ = 0;
    QString selectedBeforeFilter_;
    void rememberSelection() { selectedBeforeFilter_ = current() ? current()->key() : QString(); }
    void setGameIconSize(int idx, bool save);
    int gameIconSize_ = 3;
    QString columnsFile() const;
    void applyColumns();
    void saveColumns();
    QSize toolIconSize_{48, 48};
    Qt::WindowStates stateBeforeGame_;

    std::unique_ptr<Ui::FormMain> ui_;
    Frontend& fe_;
    SystemId sys_ = SystemId::MAME;
    ElDatabase db_;
    GamesModel* model_ = nullptr;
    GamesFilter* filter_ = nullptr;
    QTreeView* list_ = nullptr;
    Menus menus_;
    QPointer<QProcess> running_;
    MiscFilters::MultiSlot multiSlot_;           // arcade\games\mame_multicart_neogeo.ini, _stv.ini
    QList<GamesFiltersDialog::Filter> arcadeFilters_; // arcade\filters\mame_filters.ini
    VideoPreviewSettings video_;
    QString videoFile_;                          // the selected game's video ("" = none)
    QString videoGame_;                          // the game videoFile_ is for (AutoPlay once per game)
    bool videoPlaying_ = false;                  // PlayDummyVideoPreview: a video was started
    QPointer<VideoPanel> videoPanel_;            // the built-in player, over a picture panel
    QTimer* videoTimer_ = nullptr;               // AutoPlay once the selection settles
    bool videoByUser_ = false;                   // the video was asked for (not AutoPlay): say why it failed
};

// F1 anywhere in EmuLoader: the guide (MainWindowToolBar.cpp)
class GuideKey : public QObject {
public:
    explicit GuideKey(MainWindow* main) : QObject(main), main_(main) {}
protected:
    bool eventFilter(QObject* o, QEvent* e) override;
private:
    MainWindow* main_;
};

} // namespace eld
