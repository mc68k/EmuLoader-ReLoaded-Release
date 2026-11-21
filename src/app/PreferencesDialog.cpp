#include "PreferencesDialog.h"
#include <QMessageBox>
#include <QCoreApplication>
#include "Language.h"
#include "ElWidgets.h"
#include "VclRuntime.h"
#include "WinIni.h"
#include "TextFile.h"
#include <QSignalBlocker>
#include <QFileInfo>
#include <QDir>
#include <QFileDialog>
#include "ui_FormPreferences.h"
#include "SettingsSearch.h"
#include "GameDocs.h"
#include <QUrl>
#include <QDesktopServices>
#include <QFontDialog>
#include <QTreeView>
#include <QStandardItemModel>

#include <QAbstractButton>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>

namespace eld {

// The check boxes that work in this port: control, EmuLoader.ini section and key, default.
const std::vector<PreferencesDialog::Setting>& PreferencesDialog::settings() {
    static const std::vector<Setting> s = {
        // General
        {"DisableMinimize", "Preferences", "DisableMinimize", false},
        {"AllowOnlyOneInstance", "Preferences", "AllowOneInstance", true},
        {"IgnoreExitCode1InvalidFunction", "Preferences", "IgnoreExitCode1InvalidFunction", true},
        {"HideDOSBoxEmuVersionCreateGames", "Configuration", "HideDOSBoxEmuVersionCreateGames", true},
        {"HideDOSBoxRunGame", "Configuration", "HideDOSBoxRunGame", false},
        // Games List / Columns
        {"DisableNaturalSorting", "Preferences", "DisableNaturalSorting", false},
        {"ShowGrayscaleIconGamesMissROMsCHDs", "Preferences", "ShowGrayscaleIconGamesMissROMsCHDs", false},
        {"LastPlayedHideSeconds", "Preferences", "HideSecondsLastPlayed", false},
        {"TotalPlayTimeHideSeconds", "Preferences", "HideSecondsTotalPlaytime", false},
        {"ShowDriverPath", "EmuLoaderRL", "ShowDriverPath", true},
        // Details / Grouped View Settings (the status columns)
        {"DisableDriverStatusIcons", "Preferences", "DisableStatusIcons", false},
        {"HideDriverStatusTexts", "Preferences", "ShowHideStatusTexts", false},
        {"DriverStatusShowFirstLetterOnly", "Preferences", "ShowStatusFirstLetterOnly", false},
        {"ShortDriverColumnTitles", "Preferences", "ShortDriverStatusColumnTitles", false},
        {"DisableCloneIndent", "Preferences", "DisableCloneIndent", false},
        {"GameMultilineCaptions", "Preferences", "GameMultilineCaptions", false},
        {"DisableDeleteSelectedGames", "Preferences", "DisableDeleteSelectedGames", false},
        {"LeftAlignEmulatorGameTextMessageBox", "Preferences", "LeftAlignEmulatorGameTextMessageBox", false},
        {"EnableDiagnosticLog", "Preferences", "DiagnosticLog", false},
    };
    return s;
}

bool PreferencesDialog::consoleComputerSupport(Frontend& fe) {
    return fe.ini().readBool("EmuLoaderRL", "ConsoleComputerSupport", false);
}

PreferencesDialog::PreferencesDialog(Frontend& fe, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormPreferences), fe_(fe) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("Preferences"));

    // This window is never shown: the Settings window takes its controls for its tabs (takePages).

    // New: the Driver Name column with MAME's folder ("sony/zn1.cpp").
    auto* driverPath = new QCheckBox(tr("Show Driver Path"), this);
    driverPath->setObjectName("ShowDriverPath");
    driverPath->setFont(ui_->LastPlayedHideSeconds->font());
    driverPath->setToolTip(tr("Show drivers with their path in MAME's source (sony/zn1.cpp) or the file name only (zn1.cpp).\n"
                              "Older games lists have file names only: create the list again to get the paths."));

    // New settings for the General tab.
    consoleComputer_ = new QCheckBox(tr("Enable Console and Computer Support"), this);
    consoleComputer_->setObjectName("ConsoleComputerSupport");
    consoleComputer_->setFont(ui_->DisableMinimize->font());
    consoleComputer_->setToolTip(tr("MAME consoles and computers: list them, show the Machines filter button and\n"
                                 "the console.ini / computer.ini / othersys.ini custom settings"));
    auto* hint = new QLabel(tr("MAME console and computer machines"), this);
    hint->setObjectName("ConsoleComputerSupportLabel");
    hint->setFont(ui_->IgnoreExitCode1InvalidFunctionLabel->font());
    hint->setPalette(ui_->IgnoreExitCode1InvalidFunctionLabel->palette());
    hint->setStyleSheet(ui_->IgnoreExitCode1InvalidFunctionLabel->styleSheet());
    hint->setForegroundRole(ui_->IgnoreExitCode1InvalidFunctionLabel->foregroundRole());
    // The weekly update check (MainWindowUpdates.cpp)
    checkForUpdates_ = new QCheckBox(tr("Check for Updates Once a Week"), this);
    checkForUpdates_->setObjectName("CheckForUpdates");
    checkForUpdates_->setFont(ui_->DisableMinimize->font());
    checkForUpdates_->setToolTip(tr("A few seconds after EmuLoader starts, once a week, asks GitHub whether there's a newer version"));
    // Language (lang\<Name>.ini), used from the next start.
    auto* langLabel = new QLabel(tr("Language"), this);
    langLabel->setObjectName("LanguageLabel");
    langLabel->setFont(ui_->DisableMinimize->font());
    language_ = new QComboBox(this);
    language_->setObjectName("Language");
    QString appDir = programFolder();
    QString cur = fe_.ini().readString("EmuLoaderRL", "Language", "English");
    for (const LanguageFile::Info& i : LanguageFile::available({QDir(appDir).filePath("lang"), QDir(fe_.dataDir()).filePath("lang")})) {
        language_->addItem(i.name, i.id);
        if (i.matches(cur)) language_->setCurrentIndex(language_->count() - 1);
    }
    language_->setToolTip(tr("The language of EmuLoader's windows and messages (lang\\<language code>.ini).\n"
                             "It's used from the next start."));
    langLabel->setToolTip(language_->toolTip());
    initialLanguage_ = language_->currentData().toString();

    // Load (TFormMain.LoadSettings): the settings that work in this port, same keys and defaults.
    WinIni& ini = fe_.ini();
    for (const Setting& st : settings()) {
        auto* c = findChild<QCheckBox*>(st.control);
        if (c) c->setChecked(ini.readBool(st.section, st.key, st.def));
    }
    consoleComputer_->setChecked(consoleComputerSupport(fe_));
    checkForUpdates_->setChecked(ini.readBool("EmuLoaderRL", "CheckForUpdates", true));
    // (The original's Fix RetroArch Image File Names was for its console games named after their
    // files, which this version doesn't have, and Disable Threaded Preview Images Loading its way
    // round its threads' trouble: the pictures are read in the background here. Both are gone.)
    // Enable Diagnostic Log (hidden in its form): EmuLoaderRL\diagnostic.log
    ui_->EnableDiagnosticLog->show();
    ui_->EnableDiagnosticLog->setToolTip(tr("Write EmuLoaderRL\\diagnostic.log: what EmuLoader does, a line each. The --debug-log switch keeps it on whatever this setting says."));
    setupGameDocs();
    // MAMu_ icons folder: icons_directory (MAME's ui.ini, else sysimagefolders_arcade.ini)
    ui_->MAMu_IconsFolder->setText(fe_.iconsSetting());
    connect(ui_->MAMu_IconsFolderButtonSelect, &QPushButton::clicked, this, [this] {
        const QString base = fe_.mameFolder(fe_.emulator(SystemId::MAME).isSet() ? SystemId::MAME : SystemId::HBMAME);
        const QStringList now = fe_.iconFolders();
        QString dir = QFileDialog::getExistingDirectory(this, tr("MAMu_ Icons Folder"), now.value(0));
        if (dir.isEmpty()) return;
        const QString rel = QDir(base).relativeFilePath(dir);
        ui_->MAMu_IconsFolder->setText(!base.isEmpty() && !rel.startsWith("..") && !QDir::isAbsolutePath(rel)
                                           ? QDir::toNativeSeparators(rel) : QDir::toNativeSeparators(dir));
    });
    // Game manuals (PDF): arcade\sysimagefolders_arcade.ini [MAME] manuals_directory, as the original
    ui_->MAMEGameManualsPDFFolder->setText(fe_.manualsSetting());
    connect(ui_->MAMEGameManualsPDFFolderButtonDefault, &QPushButton::clicked, this,
            [this] { ui_->MAMEGameManualsPDFFolder->setText("manuals"); });
    connect(ui_->MAMEGameManualsPDFFolderButtonSelect, &QPushButton::clicked, this, [this] {
        const QString base = fe_.mameFolder(fe_.emulator(SystemId::MAME).isSet() ? SystemId::MAME : SystemId::HBMAME);
        QString dir = QFileDialog::getExistingDirectory(this, tr("Game Manuals (PDF) Folder"), fe_.manualsFolder());
        if (dir.isEmpty()) return;
        // inside MAME's folder: kept relative, as the original writes it
        const QString rel = QDir(base).relativeFilePath(dir);
        ui_->MAMEGameManualsPDFFolder->setText(!base.isEmpty() && !rel.startsWith("..") && !QDir::isAbsolutePath(rel)
                                                   ? QDir::toNativeSeparators(rel) : QDir::toNativeSeparators(dir));
    });
    ui_->InternetGameInfoLink->setText(ini.readString("Preferences", "InternetGameInfoLink", "http://www.progettoemma.net/gioco.php?game=%s"));
    ui_->InternetMAMESoftwareListGameInfoLink->setText(
        ini.readString("Preferences", "InternetGameSoftwareListInfoLink", "http://www.progettoemma.net/mess/gioco.php?game=%s&list=%s"));
    // Internet Game Info opens a page in the browser here (the original showed it in a panel, which
    // Restore at Startup turned on again). Its place: the page made from your files, or the website.
    {
        QCheckBox* local = ui_->RestoreInternetGameInfoStartup;
        local->setText(tr("Use a Page Made From Your Files (No Internet)"));
        local->setToolTip(tr("Internet Game Info shows a page made from the games list, the game documents and your pictures "
                             "(EmuLoaderRL\\gameinfo), instead of the website below"));
        local->setChecked(ini.readInteger("Preferences", "InternetGameInfoLocal", 1) != 0);
        const struct { ElButton* b; QLineEdit* e; const char* link; } defaults[] = {
            {ui_->InternetGameInfoProgettoEMMALinkButtonDefault, ui_->InternetGameInfoLink, "http://www.progettoemma.net/gioco.php?game=%s"},
            {ui_->InternetMAMESoftwareListGameInfoProgettoEMMALinkButtonDefault, ui_->InternetMAMESoftwareListGameInfoLink,
             "http://www.progettoemma.net/mess/gioco.php?game=%s&list=%s"},
            {ui_->InternetGameInfoArcadeItaliaLinkButtonDefault, ui_->InternetGameInfoLink, "http://adb.arcadeitalia.net/?mame=%s"},
            {ui_->InternetMAMESoftwareListGameInfoArcadeItaliaLinkButtonDefault, ui_->InternetMAMESoftwareListGameInfoLink,
             "http://adb.arcadeitalia.net/?mess=%s&list=%s"}};
        for (const auto& d : defaults) {
            QLineEdit* e = d.e;
            const QString link = QString::fromLatin1(d.link);
            d.b->setToolTip(link);
            connect(d.b, &QPushButton::clicked, this, [e, link] { e->setText(link); });
        }
    }

    // The original hid MAME's whole window (SW_HIDE); this port hides only its console window,
    // so the warning about windowed mode no longer applies.

    setupPage2();
    { // the website's links and buttons: only when the local page isn't used
        auto sync = [this] {
            const bool web = !ui_->RestoreInternetGameInfoStartup->isChecked();
            for (QWidget* w : std::initializer_list<QWidget*>{ui_->InternetGameInfoLink, ui_->InternetMAMESoftwareListGameInfoLink,
                                                             ui_->InternetGameInfoProgettoEMMALinkButtonDefault,
                                                             ui_->InternetMAMESoftwareListGameInfoProgettoEMMALinkButtonDefault,
                                                             ui_->InternetGameInfoArcadeItaliaLinkButtonDefault,
                                                             ui_->InternetMAMESoftwareListGameInfoArcadeItaliaLinkButtonDefault})
                w->setEnabled(web);
        };
        sync();
        connect(ui_->RestoreInternetGameInfoStartup, &QCheckBox::toggled, this, sync);
    }
    retext();
    // Tool tips (the original form has none on these).
    static const std::pair<const char*, const char*> tips[] = {
        {"DisableDeleteSelectedGames", QT_TR_NOOP("Delete, Copy and Move in the games list's right-click menu show a message instead of changing any files")},
        {"GameMultilineCaptions", QT_TR_NOOP("Long titles on two lines in the games list (Large, Extra Large and Mega Large icons) and the Machines List panel (Mega Large)")},
        {"LeftAlignEmulatorGameTextMessageBox", QT_TR_NOOP("The game and emulator lines of the Run Game Confirmation Dialog on the left instead of centered")},
        {"DisableCloneIndent", QT_TR_NOOP("Grouped view: clones not indented under their parent")},
        {"DisableMinimize", QT_TR_NOOP("Keep EmuLoader's window open while a game runs instead of minimizing it")},
        {"AllowOnlyOneInstance", QT_TR_NOOP("Starting EmuLoader again brings the running window to the front instead of opening a second copy")},
        {"IgnoreExitCode1InvalidFunction", QT_TR_NOOP("Don't report MAME's exit code 1 (\"invalid function\") as an error after a game ends")},
        {"HideDOSBoxEmuVersionCreateGames", QT_TR_NOOP("Hide MAME's command prompt window while reading its version and creating the games list")},
        {"HideDOSBoxRunGame", QT_TR_NOOP("Hide MAME's command prompt (console) window while a game runs.\n"
                              "Unlike the original, the game window itself is never hidden.")},
        {"DisableNaturalSorting", QT_TR_NOOP("Sort titles like Windows Explorer's plain word sort ('88 Games after 005)\n"
                                  "instead of natural number order (2 before 10)")},
        {"ShowGrayscaleIconGamesMissROMsCHDs", QT_TR_NOOP("Gray game icons for sets with missing ROMs or CHDs")},
        {"LastPlayedHideSeconds", QT_TR_NOOP("Show the Last Played dates without the seconds")},
        {"TotalPlayTimeHideSeconds", QT_TR_NOOP("Show the Playtime totals without the seconds")},
        {"DisableDriverStatusIcons", QT_TR_NOOP("Show the driver status columns as text only, without the colored icons")},
        {"HideDriverStatusTexts", QT_TR_NOOP("Show the driver status columns as icons only")},
        {"DriverStatusShowFirstLetterOnly", QT_TR_NOOP("Show only the first letter of each driver status text")},
        {"ShortDriverColumnTitles", QT_TR_NOOP("Short status column titles (Drv, Emu, Clr, Snd, Grp), for narrower columns")},
        {"GameSelectionGradientBar", QT_TR_NOOP("Draw the selection bar as a top-to-bottom gradient")},
        {"GameSelectionAlphaBlend", QT_TR_NOOP("Make the selection bar semi-transparent over the list background")},
        {"GameSelectionRoundCorners", QT_TR_NOOP("Round the corners of the selection bar")},
        {"GamesBackgroundImageEnable", QT_TR_NOOP("Show a picture behind the games list")},
        {"GamesTileBackground", QT_TR_NOOP("Repeat the background picture; otherwise it's stretched to fill the list")},
        {"GamesListStatusBarGradientBar", QT_TR_NOOP("Draw the games list status bar as a gradient")},
        {"ToolBarGradientBar", QT_TR_NOOP("Draw the tool bar background as a gradient (with Windows Theme off)")},
        {"ToolBarWindowsTheme", QT_TR_NOOP("Use the plain Windows tool bar background")},
        {"HintBox_FrameEnabled", QT_TR_NOOP("Draw a frame around the image hint box")},
        {"HintBox_TextShadowEnabled", QT_TR_NOOP("Draw the hint box text with a shadow")},
        {"HintBox_FontItalicStyle", QT_TR_NOOP("Italic hint box text")},
        {"HintBox_Opacity", QT_TR_NOOP("How opaque the image hint box is (0 = invisible, 255 = solid)")},
        {"HintBox_Position", QT_TR_NOOP("Where the hint box sits over the image (shown while the mouse is over it)")},
    };
    for (const auto& [name, tip] : tips)
        if (QWidget* w = findChild<QWidget*>(name); w && w->isEnabled()) w->setToolTip(tr(tip));

}

// Find a Setting: every setting of the tabs (takePages), with its tab and group.
void PreferencesDialog::addSettingsTo(QList<SettingEntry>& out) {
    for (const auto& [title, page] : taken_)
        if (page)
            SettingsSearch::addWindow(out, page, "preferences", tr("Settings"), [&title = title, page = page.data()](QWidget* w) {
                QStringList where{title};
                QString g = SettingsSearch::groupOf(w, page);
                if (!g.isEmpty()) where << g;
                return where;
            });
}

// Take me there: the control, highlighted (the Settings window shows its tab).
QWidget* PreferencesDialog::reveal(const QString& objectName) {
    QWidget* w = find(objectName);
    if (w) SettingsSearch::highlight(w);
    return w;
}

// A control by name, on a tab the Settings window took (takePages)
QWidget* PreferencesDialog::find(const QString& objectName) const {
    for (const auto& [title, page] : taken_)
        if (page)
            if (QWidget* w = page->objectName() == objectName ? page.data() : page->findChild<QWidget*>(objectName)) return w;
    return nullptr;
}

// The tabs the Settings window didn't take (a window made only for Find a Setting)
PreferencesDialog::~PreferencesDialog() {
    for (const auto& [title, page] : taken_)
        if (page && !page->parent()) delete page.data();
}


// Page 2 - Light Mode (ini_files\lightmode.ini). Changes show in the main window at once.
void PreferencesDialog::setupPage2() {
    lm_.load(fe_.iniFilesFolder() + "lightmode.ini");
    fillPage2();
    auto changed = [this] { emit lightModeChanged(lm_); };
    for (const auto& f : LightMode::intFields())
        if (auto* cb = findChild<ElColorBox*>(f.control)) {
            auto member = f.member;
            connect(cb, &ElColorBox::colorChanged, this, [this, cb, member, changed] {
                lm_.*member = cb->tcolor();
                changed();
            });
        }
    for (const auto& f : LightMode::boolFields())
        if (auto* c = findChild<QCheckBox*>(f.control)) {
            auto member = f.member;
            connect(c, &QCheckBox::toggled, this, [this, member, changed](bool on) {
                lm_.*member = on;
                changed();
            });
        }
    connect(ui_->GamesListSplitterStyleSelector, QOverload<int>::of(&QComboBox::activated), this, [this, changed](int i) {
        lm_.splitterStyle = i;
        changed();
    });
    connect(ui_->GamesBackgroundImage, &QLineEdit::editingFinished, this, [this] { lm_.listImage = ui_->GamesBackgroundImage->text(); });
    connect(ui_->GamesBackgroundImageButtonUpdate, &QPushButton::clicked, this, [this, changed] {
        lm_.listImage = ui_->GamesBackgroundImage->text();
        changed();
    });
    connect(ui_->GamesBackgroundImageButtonSelect, &QPushButton::clicked, this, [this, changed] {
        QString f = QFileDialog::getOpenFileName(this, ui_->GamesBackgroundImageEnable->text(),
                                                 QFileInfo(resolvePath(fe_.dataDir(), lm_.listImage)).absolutePath(),
                                                 tr("PNG images (*.png);;Images (*.png *.jpg *.bmp)"));
        if (f.isEmpty()) return;
        QString rel = QDir(fe_.dataDir()).relativeFilePath(f);
        lm_.listImage = QDir::toNativeSeparators(rel.startsWith("..") ? f : rel);
        ui_->GamesBackgroundImage->setText(lm_.listImage);
        changed();
    });
    // Default buttons (the *ButtonDefaultClick handlers): the form's DefaultColorColor values.
    auto defaults = [this, changed](std::initializer_list<const char*> boxes, std::function<void()> extra = {}) {
        for (const char* n : boxes)
            if (auto* cb = findChild<ElColorBox*>(n)) {
                cb->setToDefault();
                for (const auto& f : LightMode::intFields())
                    if (QString(f.control) == n) lm_.*(f.member) = cb->tcolor();
            }
        if (extra) extra();
        fillPage2();
        changed();
    };
    connect(ui_->ToolBarBkColorsDefaultButton, &QPushButton::clicked, this, [this, defaults] {
        defaults({"ToolBarBkTopColor", "ToolBarBkBottomColor"}, [this] { lm_.toolBarWindowsTheme = lm_.toolBarGradient = true; });
    });
    connect(ui_->SearchGamesPanelColorsButtonDefault, &QPushButton::clicked, this, [defaults] {
        defaults({"SearchGamesPanelFilterFontColor", "SearchGamesPanelFieldFontColor", "SearchGamesPanelEditBoxFontColor",
                  "SearchGamesPanelEditBoxBackgroundColor"});
    });
    connect(ui_->GameSelectionButtonDefault, &QPushButton::clicked, this, [this, defaults] {
        defaults({"GamesSelectionTopColor", "GamesSelectionBottomColor", "GamesSelectionFrameColor", "GamesSelectionFontColor",
                  "GamesSelectionMissROMsTopColor", "GamesSelectionMissROMsBottomColor", "GamesSelectionMissROMsFrameColor",
                  "GamesSelectionMissROMsFontColor", "GamesSelectionInactiveTopColor", "GamesSelectionInactiveBottomColor",
                  "GamesSelectionInactiveFrameColor", "GamesSelectionInactiveFontColor"},
                 [this] { lm_.selGradient = lm_.selAlphaBlend = lm_.selRoundCorners = true; });
    });
    connect(ui_->GamesBackgroundColorButtonDefault, &QPushButton::clicked, this, [defaults] { defaults({"GamesBackgroundColor"}); });
    connect(ui_->GamesListSplitterStyleSelectorButtonDefault, &QPushButton::clicked, this,
            [this, defaults] { defaults({}, [this] { lm_.splitterStyle = 6; }); });
    connect(ui_->GamesListSplitterSingleColorButtonDefault, &QPushButton::clicked, this, [defaults] {
        defaults({"GamesListSplitterSingleColor", "GamesListSplitterSingleColorHot"});
    });
    connect(ui_->GamesListStatusBarButtonDefault, &QPushButton::clicked, this, [this, defaults] {
        defaults({"GamesListStatusBarTopColor", "GamesListStatusBarBottomColor", "GamesListStatusBarFontColor"},
                 [this] { lm_.statusGradient = true; });
    });
    connect(ui_->ImageBorderColorButtonDefault, &QPushButton::clicked, this, [defaults] { defaults({"ImageBorderColor"}); });
    // MAME Game Docs Colors: GameDocumentsButtonDefaultClick (font and background), border, font label
    connect(ui_->GameDocumentsButtonDefault, &QPushButton::clicked, this, [this, defaults] {
        defaults({"GameDocumentsBackgroundColor"}, [this] {
            lm_.setDocsFont(QFont("Calibri", 9));
            lm_.docsFontColor = 0x000000;
        });
    });
    connect(ui_->GameDocsBorderColorButtonDefault, &QPushButton::clicked, this, [defaults] { defaults({"GameDocsBorderColor"}); });
    ui_->GameDocsFont_Setting->setCursor(Qt::PointingHandCursor);
    ui_->GameDocsFont_Setting->setToolTip(tr("Click to choose the font of the game documents"));
    ui_->GameDocsFont_Setting->installEventFilter(this);
    // Image layout splitters
    connect(ui_->ImageSplitterStyleSelector, QOverload<int>::of(&QComboBox::activated), this, [this, changed](int i) {
        lm_.imgSplitterStyle = i;
        changed();
    });
    connect(ui_->ImageSplitterStyleSelectorButtonDefault, &QPushButton::clicked, this,
            [this, defaults] { defaults({}, [this] { lm_.imgSplitterStyle = 1; }); });
    connect(ui_->ImageSplitterSingleColorButtonDefault, &QPushButton::clicked, this,
            [defaults] { defaults({"ImageSplitterSingleColor", "ImageSplitterSingleColorHot"}); });
    // Image hint box
    connect(ui_->HintBox_Opacity, &QSlider::valueChanged, this, [this, changed](int v) {
        lm_.hintOpacity = v;
        updateHintLabels();
        changed();
    });
    connect(ui_->HintBox_Position, &QSlider::valueChanged, this, [this, changed](int v) {
        lm_.hintPosition = v;
        updateHintLabels();
        changed();
    });
    connect(ui_->HintBox_OpacityButtonDefault, &QPushButton::clicked, this,
            [this, defaults] { defaults({}, [this] { lm_.hintOpacity = 200; }); });
    connect(ui_->HintBoxtColorsButtonDefault, &QPushButton::clicked, this, [this, defaults] {
        defaults({"HintBox_Color", "HintBox_FrameColor"}, [this] { lm_.hintPosition = 4; lm_.hintFrame = true; });
    });
    connect(ui_->HintTextDefaultColorsButtonDefault, &QPushButton::clicked, this, [this, defaults] {
        defaults({"HintBox_TextColor", "HintBox_TextShadowColor"}, [this] { lm_.hintShadowOn = true; lm_.hintItalic = false; });
    });
}

void PreferencesDialog::fillPage2() {
    // The game documents font (shown on the "Font" label, in the background color).
    ui_->GameDocsFont_Setting->setFont(lm_.docsFont());
    ui_->GameDocsFont_Setting->setStyleSheet(QString("QLabel { color: %1; background: %2; }")
                                                 .arg(LightMode::qc(lm_.docsFontColor).name(), LightMode::qc(lm_.docsBack).name()));
    {
        QSignalBlocker b1(ui_->HintBox_Opacity), b2(ui_->HintBox_Position);
        vcl::setSliderValue(ui_->HintBox_Opacity, lm_.hintOpacity);
        vcl::setSliderValue(ui_->HintBox_Position, lm_.hintPosition);
    }
    updateHintLabels();
    for (const auto& f : LightMode::intFields())
        if (auto* cb = findChild<ElColorBox*>(f.control)) cb->setTColor(lm_.*(f.member));
    for (const auto& f : LightMode::boolFields())
        if (auto* c = findChild<QCheckBox*>(f.control)) {
            QSignalBlocker b(c);
            c->setChecked(lm_.*(f.member));
        }
    ui_->GamesListSplitterStyleSelector->setCurrentIndex(std::clamp(lm_.splitterStyle, 0, ui_->GamesListSplitterStyleSelector->count() - 1));
    ui_->ImageSplitterStyleSelector->setCurrentIndex(std::clamp(lm_.imgSplitterStyle, 0, ui_->ImageSplitterStyleSelector->count() - 1));
    ui_->GamesBackgroundImage->setText(lm_.listImage);
}

void PreferencesDialog::updateHintLabels() {
    static const char* pos[] = {QT_TR_NOOP("Top/Left"), QT_TR_NOOP("Top/Middle"), QT_TR_NOOP("Top/Right"),
                                QT_TR_NOOP("Middle/Left"), QT_TR_NOOP("Middle/Right"), QT_TR_NOOP("Bottom/Left"),
                                QT_TR_NOOP("Bottom/Middle"), QT_TR_NOOP("Bottom/Right")};
    ui_->HintBox_OpacityLabel->setText(tr("Opacity [%1]").arg(lm_.hintOpacity));
    ui_->HintBox_PositionLabel->setText(tr(pos[std::clamp(lm_.hintPosition, 1, 8) - 1]));
}

// MAME Game Docs (page 1): Display Mode, Display Order (with Up / Down / Reset) and the links to
// the DAT files' sites. Read from el_extras.ini, written when the window closes.
void PreferencesDialog::setupGameDocs() {
    WinIni ex;
    ex.load(fe_.userFile("el_extras.ini"));
    bool tabs = ex.readInteger("GameDocuments", "DisplayMode", 0) == 1;
    ui_->GameDocsDisplayModeTabs->setChecked(tabs);
    ui_->GameDocsDisplayModeSinglePanel->setChecked(!tabs);
    // ELV_AddGameDocs
    static const char* names[] = {nullptr, QT_TR_NOOP("Info"), QT_TR_NOOP("History"), QT_TR_NOOP("Driver Info"),
                                  QT_TR_NOOP("MAMEScore (Story)"), QT_TR_NOOP("MAME Action Replay Page"),
                                  QT_TR_NOOP("Game Initialization")};
    docsModel_ = new QStandardItemModel(this);
    auto fill = [this](const QList<QPair<int, bool>>& order) {
        docsModel_->clear();
        for (const auto& [doc, on] : order) {
            auto* it = new QStandardItem(tr(names[doc]));
            it->setData(doc, Qt::UserRole);
            it->setCheckable(true);
            it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
            it->setEditable(false);
            docsModel_->appendRow(it);
        }
    };
    fill(GameDocs::readOrder(ex));
    QTreeView* view = ui_->GameDocs;
    view->setModel(docsModel_);
    view->setHeaderHidden(true);
    view->setRootIsDecorated(false);
    view->setToolTip(tr("The documents shown, in this order; untick one to leave it out"));
    // GameDocsItemPaintText: unticked ones in gray
    connect(docsModel_, &QStandardItemModel::itemChanged, this, [](QStandardItem* it) {
        it->setForeground(it->checkState() == Qt::Checked ? QBrush() : QBrush(Qt::gray));
    });
    for (int i = 0; i < docsModel_->rowCount(); ++i) emit docsModel_->itemChanged(docsModel_->item(i));
    auto move = [this, view](int delta) {
        int r = view->currentIndex().row(), t = r + delta;
        if (r < 0 || t < 0 || t >= docsModel_->rowCount()) return;
        docsModel_->insertRow(t, docsModel_->takeRow(r));
        view->setCurrentIndex(docsModel_->index(t, 0));
    };
    connect(ui_->GameDocsButtonUp, &QPushButton::clicked, this, [move] { move(-1); });
    connect(ui_->GameDocsButtonDown, &QPushButton::clicked, this, [move] { move(1); });
    connect(ui_->GameDocsButtonReset, &QPushButton::clicked, this, [this, fill] {
        QList<QPair<int, bool>> all;
        for (int d = 1; d <= GameDocs::Count; ++d) all.append({d, true});
        fill(all);
        for (int i = 0; i < docsModel_->rowCount(); ++i) emit docsModel_->itemChanged(docsModel_->item(i));
    });
    // Where to get the files (the labels under the box). MAMESCORE's site is gone.
    const std::pair<QLabel*, const char*> links[] = {
        {ui_->LabelGoToMAMEInfo, "https://mashinfo.github.io/mameinfo/"},
        {ui_->LabelGoToMESSInfo, "https://www.progettosnaps.net/messinfo/"},
        {ui_->LabelGoToGameInit, "https://www.progettosnaps.net/gameinit/"},
        {ui_->LabelGoToMARP, "http://replay.marpirc.net/txt/scores3.htm"},
        {ui_->LabelGoToMAMEHistory, "https://www.arcade-history.com/"},
        {ui_->LabelGoToMAMEScore, nullptr}};
    // The MAMu_ icons: AntoPISA's progetto-SNAPS carries on MAMu_'s collection (a link above the folder)
    {
        auto* l = new QLabel(tr("Get the icons: progetto-SNAPS (AntoPISA), carrying on MAMu_'s collection"), this);
        l->setObjectName("LabelGoToMAMuIcons");
        l->setFont(ui_->LabelGoToMESSInfo->font());
        l->setPalette(ui_->LabelGoToMESSInfo->palette());
        l->setStyleSheet(ui_->LabelGoToMESSInfo->styleSheet());
        l->setProperty("url", QStringLiteral("https://www.progettosnaps.net/icons/"));
        l->setToolTip(QStringLiteral("https://www.progettosnaps.net/icons/"));
        l->setCursor(Qt::PointingHandCursor);
        l->installEventFilter(this);
        l->show();
    }
    for (auto [label, url] : links) {
        if (!url) {
            label->setToolTip(tr("The MAMESCORE site is no longer online"));
            continue;
        }
        label->setProperty("url", QString(url));
        label->setToolTip(url);
        label->setCursor(Qt::PointingHandCursor);
        label->installEventFilter(this);
    }
}

bool PreferencesDialog::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::MouseButtonRelease) {
        auto* w = qobject_cast<QWidget*>(o);
        if (w && w->property("url").isValid()) {
            QDesktopServices::openUrl(QUrl(w->property("url").toString()));
            return true;
        }
        if (o == ui_->GameDocsFont_Setting) { // GameDocsFont_SettingClick
            bool ok = false;
            QFont f = QFontDialog::getFont(&ok, lm_.docsFont(), this, tr("Game Documents Font"));
            if (ok) {
                lm_.setDocsFont(f);
                fillPage2();
                emit lightModeChanged(lm_);
            }
            return true;
        }
    }
    return QDialog::eventFilter(o, e);
}

void PreferencesDialog::saveSettings() {
    WinIni& ini = fe_.ini();
    for (const Setting& st : settings())
        if (auto* c = qobject_cast<QCheckBox*>(find(st.control))) ini.writeInteger(st.section, st.key, c->isChecked() ? 1 : 0);
    ini.writeInteger("EmuLoaderRL", "ConsoleComputerSupport", consoleComputer_->isChecked() ? 1 : 0);
    ini.writeInteger("EmuLoaderRL", "CheckForUpdates", checkForUpdates_->isChecked() ? 1 : 0);
    ini.writeInteger("Preferences", "InternetGameInfoLocal", ui_->RestoreInternetGameInfoStartup->isChecked() ? 1 : 0);
    ini.writeString("Preferences", "InternetGameInfoLink", ui_->InternetGameInfoLink->text().trimmed());
    ini.writeString("Preferences", "InternetGameSoftwareListInfoLink", ui_->InternetMAMESoftwareListGameInfoLink->text().trimmed());
    QString lang = language_->currentData().toString();
    ini.writeString("EmuLoaderRL", "Language", lang);
    if (lang != initialLanguage_) {
        initialLanguage_ = lang;
        QMessageBox::information(this, tr("Language"), tr("The new language is used the next time EmuLoader starts."));
    }
    fe_.saveIni();
    if (ui_->MAMu_IconsFolder->text().trimmed() != fe_.iconsSetting()) {
        fe_.setIconsSetting(ui_->MAMu_IconsFolder->text().trimmed());
        emit iconsFolderChanged();
    }
    if (ui_->MAMEGameManualsPDFFolder->text().trimmed() != fe_.manualsSetting())
        fe_.setManualsSetting(ui_->MAMEGameManualsPDFFolder->text().trimmed());
    { // el_extras.ini [GameDocuments]: Display Mode and Display Order (UpdateExtraIni)
        QString file = fe_.userFile("el_extras.ini");
        WinIni ex;
        ex.load(file);
        ex.writeInteger("GameDocuments", "DisplayMode", ui_->GameDocsDisplayModeTabs->isChecked() ? 1 : 0);
        for (int i = 0; i < docsModel_->rowCount(); ++i) {
            QStandardItem* it = docsModel_->item(i);
            ex.writeString("GameDocuments", QString("DocIndex%1").arg(i + 1),
                           QString("%1%2").arg(it->data(Qt::UserRole).toInt()).arg(it->checkState() == Qt::Checked ? 1 : 0));
        }
        ex.save(file);
    }
    lm_.listImage = ui_->GamesBackgroundImage->text();
    lm_.save(fe_.iniFilesFolder() + "lightmode.ini");
}

void PreferencesDialog::closeEvent(QCloseEvent* e) {
    saveSettings();
    e->accept();
    QDialog::accept();
}

void PreferencesDialog::reject() {
    saveSettings();
    QDialog::accept();
}

void PreferencesDialog::accept() {
    saveSettings();
    QDialog::accept();
}

} // namespace eld
