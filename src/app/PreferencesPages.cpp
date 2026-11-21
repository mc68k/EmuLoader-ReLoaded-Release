// PreferencesDialog: the settings' texts and explanations, and the Settings window's tabs made
// from this window's controls (General, Games List, Game Info and Documents, Colors), laid out
// in framed groups that fill the window (SettingsLayout).
#include "PreferencesDialog.h"

#include "SettingsLayout.h"
#include "ui_FormPreferences.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QTreeView>

namespace eld {

using settingsui::Group;
using settingsui::Page;

// The texts: full words, and under the controls a sentence saying what each one does.
void PreferencesDialog::retext() {
    QLabel* model = ui_->IgnoreExitCode1InvalidFunctionLabel; // the explanations' gray
    auto explain = [model](QWidget* parent, const QString& name, const QString& text) {
        QLabel* l = parent->findChild<QLabel*>(name);
        if (!l) {
            l = new QLabel(parent);
            l->setObjectName(name);
        }
        settingsui::hint(text, model, l);
        return l;
    };
    // General
    ui_->DisableMinimize->setText(tr("Don't Minimize EmuLoader When Running Games"));
    explain(this, "DisableMinimizeLabel", tr("EmuLoader's window stays open while a game runs."));
    explain(this, "AllowOnlyOneInstanceLabel",
            tr("Starting EmuLoader again brings the open window to the front instead of opening a second copy."));
    explain(this, "IgnoreExitCode1InvalidFunctionLabel",
            tr("Some emulators report this code when they close normally. Turn this on to stop the message."));
    ui_->LeftAlignEmulatorGameTextMessageBox->setText(tr("Left Align Emulator and Game Info"));
    explain(this, "LeftAlignEmulatorGameTextMessageBoxLabel",
            tr("The Run Game confirmation shows the game and emulator lines aligned to the left instead of centered."));
    ui_->DisableDeleteSelectedGames->setText(tr("Protect Game Files (No Deleting, Copying or Moving)"));
    explain(this, "DisableDeleteSelectedGamesLabel",
            tr("Delete, Copy and Move in the games list's menu show a message instead of changing any files."));
    ui_->EnableDiagnosticLog->setText(tr("Enable Diagnostic Log"));
    explain(this, "EnableDiagnosticLogLabel",
            tr("Records what EmuLoader does in a log file (EmuLoaderRL\\diagnostic.log), to help track down problems."));
    explain(this, "ConsoleComputerSupportLabel",
            tr("Adds MAME's consoles and computers, and their software lists, to the arcade games."));
    explain(this, "LanguageHint", tr("Used from the next start."));
    explain(this, "CheckForUpdatesLabel",
            tr("A few seconds after EmuLoader starts, once a week, asks GitHub whether there's a newer version, and "
               "says so when there is. Only the version number is asked for: nothing about you or your games is sent. "
               "Help > Check for Updates asks at any time."));
    ui_->HideDOSBoxEmuVersionCreateGames->setText(tr("While Creating the Games List or Reading the Emulator Version"));
    ui_->HideDOSBoxRunGame->setText(tr("While Running a Game"));
    explain(this, "HideDOSBoxRunGameLabel", tr("The game's own window still shows."));

    // Games List
    ui_->GameMultilineCaptions->setText(tr("Multiline Captions"));
    explain(this, "GameMultilineCaptionsLabel", tr("Long titles wrap onto a second line. Best with larger icons (32×32 and up)."));
    ui_->DisableNaturalSorting->setText(tr("Disable Natural Sorting"));
    explain(this, "DisableNaturalSortingLabel",
            tr("Sorts numbers character by character (1, 10, 11, 2) instead of by value (1, 2, 10, 11)."));
    ui_->ShowGrayscaleIconGamesMissROMsCHDs->setText(tr("Show Grayscale Icons for Incomplete Games"));
    explain(this, "ShowGrayscaleIconGamesMissROMsCHDsLabel", tr("Games with missing ROM or CHD files show their icon in gray."));
    ui_->DisableCloneIndent->setText(tr("Disable Clone Indent (Grouped View)"));
    explain(this, "DisableCloneIndentLabel", tr("Clones line up with their parent instead of being indented below it."));
    ui_->DriverStatusShowFirstLetterOnly->setText(tr("Show Only the First Letter of Status Texts"));
    explain(this, "ShortDriverColumnTitlesLabel", tr("Drv, Emu, Clr, Snd and Grp, for narrower columns."));
    explain(this, "ShowDriverPathLabel",
            tr("Shows the driver with its folder in MAME's source (sony/zn1.cpp). If only file names show, create the games list again."));
    // Game documents and Internet Game Info
    explain(this, "Setting_MAMEGameDocsBoxTipLabel",
            tr("Turn game documents on or off from the games list's right-click menu, or with F3."));
    ui_->GameDocsDisplayModeSinglePanel->setText(tr("Pictures and Documents in One Panel"));
    ui_->GameDocsDisplayModeTabs->setText(tr("Pictures and Documents in Separate Panels"));
    explain(this, "GameDocsDisplayModeSinglePanelLabel", tr("The documents share the picture panel."));
    explain(this, "GameDocsDisplayModeTabsLabel", tr("The documents get a panel of their own."));
    ui_->MAMEGameManualsPDFFolderLabel->setText(tr("Folder"));
    explain(this, "MAMEGameManualsPDFFolderTipLabel",
            tr("A full path, or a folder inside MAME's folder."))
        ->setToolTip(QString());
    ui_->InternetGameInfoLinkLabel->setText(tr("Games"));
    ui_->InternetMAMESoftwareListGameInfoLinkLabel->setText(tr("Software"));
    explain(this, "RestoreInternetGameInfoStartupLabel",
            tr("Internet Game Info shows a page made from the games list, the game documents and your pictures, "
               "instead of the website below."));
    explain(this, "InternetGameInfoLinksLabel",
            tr("Put %s where the name goes: the game's short name, or for software, the software name and then its list."));

    // Colors: full words
    ui_->GamesBackgroundImageEnable->setText(tr("Use a Picture as Background (.png)"));
    const std::pair<QLabel*, const char*> colors[] = {
        {ui_->GamesSelectionTopColorLabel, QT_TR_NOOP("Top Background")},
        {ui_->GamesSelectionMissROMsTopColorLabel, QT_TR_NOOP("Top Background")},
        {ui_->GamesSelectionInactiveTopColorLabel, QT_TR_NOOP("Top Background")},
        {ui_->GamesSelectionBottomColorLabel, QT_TR_NOOP("Bottom Background")},
        {ui_->GamesSelectionMissROMsBottomColorLabel, QT_TR_NOOP("Bottom Background")},
        {ui_->GamesSelectionInactiveBottomColorLabel, QT_TR_NOOP("Bottom Background")},
        {ui_->GamesSelectionFontColorLabel, QT_TR_NOOP("Text Color")},
        {ui_->GamesSelectionMissROMsFontColorLabel, QT_TR_NOOP("Text Color")},
        {ui_->GamesSelectionInactiveFontColorLabel, QT_TR_NOOP("Text Color")},
        {ui_->GamesSelectionMissROMsTitleLabel, QT_TR_NOOP("Games With Missing ROMs or CHDs")},
        {ui_->GamesListStatusBarFontColorLabel, QT_TR_NOOP("Text Color")},
        {ui_->SearchGamesPanelFilterFontColorLabel, QT_TR_NOOP("Filter Text")},
        {ui_->SearchGamesPanelFieldFontColorLabel, QT_TR_NOOP("Field Text")},
        {ui_->SearchGamesPanelEditBoxFontColorLabel, QT_TR_NOOP("Edit Box Text")},
        {ui_->SearchGamesPanelEditBoxBackgroundColorLabel, QT_TR_NOOP("Edit Box Background")},
        {ui_->GameDocumentsBackgroundColorLabel, QT_TR_NOOP("Background")},
        {ui_->HintBox_ColorLabel, QT_TR_NOOP("Background Color")},
        {ui_->HintBox_TextColorLabel, QT_TR_NOOP("Text Color")},
    };
    for (const auto& [l, text] : colors) l->setText(tr(text));
}

// The Settings window's tabs, made from this window's controls (they move there). A tab's group
// takes the name of the original's box, so Find a Setting and "open at" find the group.
QList<QPair<QString, QWidget*>> PreferencesDialog::takePages() {
    QList<QPair<QString, QWidget*>> out;
    auto named = [this](const char* n) { return findChild<QLabel*>(n); };
    auto add = [this, &out](const QString& title, QWidget* page) {
        taken_.append({title, page});
        out.append({title, page});
    };

    // General
    {
        Group startup(tr("Startup"));
        startup.check(ui_->AllowOnlyOneInstance, named("AllowOnlyOneInstanceLabel"))
            .check(consoleComputer_, named("ConsoleComputerSupportLabel"))
            .space(2)
            .row(findChild<QLabel*>("LanguageLabel"), language_)
            .row(nullptr, named("LanguageHint"))
            .check(checkForUpdates_, named("CheckForUpdatesLabel"));
        Group trouble(tr("Troubleshooting"));
        trouble.check(ui_->EnableDiagnosticLog, named("EnableDiagnosticLogLabel"));
        Group running(tr("Running Games"));
        running.check(ui_->DisableMinimize, named("DisableMinimizeLabel"))
            .check(ui_->IgnoreExitCode1InvalidFunction, ui_->IgnoreExitCode1InvalidFunctionLabel)
            .check(ui_->LeftAlignEmulatorGameTextMessageBox, ui_->LeftAlignEmulatorGameTextMessageBoxLabel);
        Group dos(tr("Hide MAME's Command Prompt Window"), "HideDOSBoxBox");
        dos.check(ui_->HideDOSBoxEmuVersionCreateGames).check(ui_->HideDOSBoxRunGame, ui_->HideDOSBoxRunGameLabel);
        Group protect(tr("Protect Game Files"), "Settings_GeneralBox");
        protect.check(ui_->DisableDeleteSelectedGames, ui_->DisableDeleteSelectedGamesLabel);
        Page p("PreferencesPage1", tr("How EmuLoader starts, runs games and protects your files."), 2);
        p.add(0, startup).add(0, trouble).add(1, running).add(1, dos).add(1, protect).finish();
        add(tr("General"), p.widget());
    }

    // Games List
    {
        Group display(tr("Display"), "GamesListBox");
        display.check(ui_->GameMultilineCaptions, ui_->GameMultilineCaptionsLabel)
            .check(ui_->DisableNaturalSorting, ui_->DisableNaturalSortingLabel)
            .check(ui_->ShowGrayscaleIconGamesMissROMsCHDs, ui_->ShowGrayscaleIconGamesMissROMsCHDsLabel)
            .check(ui_->DisableCloneIndent, named("DisableCloneIndentLabel"));
        Group status(tr("Driver Status Columns"));
        status.check(ui_->DisableDriverStatusIcons)
            .check(ui_->HideDriverStatusTexts)
            .check(ui_->DriverStatusShowFirstLetterOnly)
            .check(ui_->ShortDriverColumnTitles, named("ShortDriverColumnTitlesLabel"));
        Group other(tr("Other Columns"));
        other.check(ui_->LastPlayedHideSeconds)
            .check(ui_->TotalPlayTimeHideSeconds)
            .check(findChild<QCheckBox*>("ShowDriverPath"), named("ShowDriverPathLabel"));
        Group icons(tr("MAMu_ Icons (MAME and Arcade Only)"), "MAMu_IconsFolderBox");
        icons.wide(named("LabelGoToMAMuIcons")).row(ui_->MAMu_Icon, ui_->MAMu_IconsFolder, {ui_->MAMu_IconsFolderButtonSelect});
        ui_->MAMu_Icon->setFixedSize(32, 32);
        Page p("PreferencesPage2", tr("How the games list sorts and shows the games, and where its icons come from."), 2);
        p.add(0, display).add(1, status).add(1, other).full(icons).finish();
        add(tr("Games List"), p.widget());
    }

    // Game Info and Documents
    {
        Group docs(tr("Game Documents"), "Setting_MAMEGameDocsBox");
        docs.text(ui_->Setting_MAMEGameDocsBoxTipLabel)
            .space(2)
            .check(ui_->GameDocsDisplayModeSinglePanel, ui_->GameDocsDisplayModeSinglePanelLabel)
            .check(ui_->GameDocsDisplayModeTabs, ui_->GameDocsDisplayModeTabsLabel);
        Group order(tr("Display Order"), "GameDocsDisplayOrderBox");
        order.wide(ui_->GameDocs).line({ui_->GameDocsButtonUp, ui_->GameDocsButtonDown}, ui_->GameDocsButtonReset);
        ui_->GameDocs->setMinimumHeight(140);
        Group files(tr("Where to Get the Files"));
        for (QLabel* l : {ui_->LabelGoToMAMEHistory, ui_->LabelGoToMAMEInfo, ui_->LabelGoToMESSInfo, ui_->LabelGoToGameInit,
                          ui_->LabelGoToMARP, ui_->LabelGoToMAMEScore}) {
            l->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            files.wide(l);
        }
        Group manuals(tr("Game Manuals (PDF)"));
        manuals.row(ui_->MAMEGameManualsPDFFolderLabel, ui_->MAMEGameManualsPDFFolder,
                    {ui_->MAMEGameManualsPDFFolderButtonSelect, ui_->MAMEGameManualsPDFFolderButtonDefault})
            .row(nullptr, ui_->MAMEGameManualsPDFFolderTipLabel);
        ui_->MAMEGameManualsPDFFolderTipLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        Group web(tr("Internet Game Info"), "InternetGameInfoBox");
        web.check(ui_->RestoreInternetGameInfoStartup, named("RestoreInternetGameInfoStartupLabel"))
            .space(2)
            .text(named("InternetGameInfoLinksLabel"))
            .row(ui_->InternetGameInfoLinkLabel, ui_->InternetGameInfoLink,
                 {ui_->InternetGameInfoProgettoEMMALinkButtonDefault, ui_->InternetGameInfoArcadeItaliaLinkButtonDefault})
            .row(ui_->InternetMAMESoftwareListGameInfoLinkLabel, ui_->InternetMAMESoftwareListGameInfoLink,
                 {ui_->InternetMAMESoftwareListGameInfoProgettoEMMALinkButtonDefault,
                  ui_->InternetMAMESoftwareListGameInfoArcadeItaliaLinkButtonDefault});
        Page p("PreferencesPage3",
               tr("The game documents shown beside the pictures, the game manuals, and the Internet Game Info page."), 2);
        p.add(0, docs).add(0, order).add(1, files).add(1, manuals).full(web).finish();
        add(tr("Game Info and Documents"), p.widget());
    }

    // Colors
    {
        Group sel(tr("Games List Selection Bar"), "GamesSelectionBarColorsBox");
        sel.line({ui_->GameSelectionGradientBar, ui_->GameSelectionAlphaBlend, ui_->GameSelectionRoundCorners},
                 ui_->GameSelectionButtonDefault)
            .row(ui_->GamesSelectionTopColorLabel, ui_->GamesSelectionTopColor)
            .row(ui_->GamesSelectionBottomColorLabel, ui_->GamesSelectionBottomColor)
            .row(ui_->GamesSelectionFrameColorLabel, ui_->GamesSelectionFrameColor)
            .row(ui_->GamesSelectionFontColorLabel, ui_->GamesSelectionFontColor)
            .space(4)
            .text(ui_->GamesSelectionMissROMsTitleLabel)
            .row(ui_->GamesSelectionMissROMsTopColorLabel, ui_->GamesSelectionMissROMsTopColor)
            .row(ui_->GamesSelectionMissROMsBottomColorLabel, ui_->GamesSelectionMissROMsBottomColor)
            .row(ui_->GamesSelectionMissROMsFrameColorLabel, ui_->GamesSelectionMissROMsFrameColor)
            .row(ui_->GamesSelectionMissROMsFontColorLabel, ui_->GamesSelectionMissROMsFontColor)
            .space(4)
            .text(ui_->GamesSelectionInactiveLabel)
            .row(ui_->GamesSelectionInactiveTopColorLabel, ui_->GamesSelectionInactiveTopColor)
            .row(ui_->GamesSelectionInactiveBottomColorLabel, ui_->GamesSelectionInactiveBottomColor)
            .row(ui_->GamesSelectionInactiveFrameColorLabel, ui_->GamesSelectionInactiveFrameColor)
            .row(ui_->GamesSelectionInactiveFontColorLabel, ui_->GamesSelectionInactiveFontColor);
        Group bar(tr("Games List Status Bar Colors"), "GamesListStatusBarColorsBox");
        bar.line({ui_->GamesListStatusBarGradientBar}, ui_->GamesListStatusBarButtonDefault)
            .row(ui_->GamesListStatusBarTopColorLabel, ui_->GamesListStatusBarTopColor)
            .row(ui_->GamesListStatusBarBottomColorLabel, ui_->GamesListStatusBarBottomColor)
            .row(ui_->GamesListStatusBarFontColorLabel, ui_->GamesListStatusBarFontColor);
        Group tool(tr("Tool Bar Background Colors"), "ToolBarBackgroundColorsBox");
        tool.line({ui_->ToolBarGradientBar, ui_->ToolBarWindowsTheme}, ui_->ToolBarBkColorsDefaultButton)
            .row(ui_->ToolBarBkTopColorLabel, ui_->ToolBarBkTopColor)
            .row(ui_->ToolBarBkBottomColorLabel, ui_->ToolBarBkBottomColor);
        Group hintBox(tr("Image Hint Box"), "ImagesHintBox");
        hintBox.row(ui_->HintBox_PositionLabel, ui_->HintBox_Position, {ui_->HintBoxtColorsButtonDefault})
            .row(ui_->HintBox_ColorLabel, ui_->HintBox_Color)
            .row(ui_->HintBox_FrameEnabled, ui_->HintBox_FrameColor)
            .row(ui_->HintBox_OpacityLabel, ui_->HintBox_Opacity, {ui_->HintBox_OpacityButtonDefault})
            .line({ui_->HintBox_FontItalicStyle}, ui_->HintTextDefaultColorsButtonDefault)
            .row(ui_->HintBox_TextColorLabel, ui_->HintBox_TextColor)
            .row(ui_->HintBox_TextShadowEnabled, ui_->HintBox_TextShadowColor);
        ui_->HintBox_PositionLabel->setMinimumWidth(ui_->HintBox_PositionLabel->fontMetrics().horizontalAdvance(tr("Bottom/Middle")) + 8);
        Group splitters(tr("Games List and Machines List Splitters"), "GamesListSplittersBox");
        splitters.row(nullptr, ui_->GamesListSplitterStyleSelector, {ui_->GamesListSplitterStyleSelectorButtonDefault})
            .line({ui_->GamesListSplitterSingleColorTitleLabel, ui_->GamesListSplitterSingleColorButtonDefault},
                  ui_->GamesListSplitterShowGripIcon)
            .row(ui_->GamesListSplitterSingleColorLabel, ui_->GamesListSplitterSingleColor)
            .row(ui_->GamesListSplitterSingleColorHotLabel, ui_->GamesListSplitterSingleColorHot);
        Group search(tr("Tool Bar Search Games Panel Colors"), "SearchGamesPanelColorsBox");
        search.line({}, ui_->SearchGamesPanelColorsButtonDefault)
            .row(ui_->SearchGamesPanelFilterFontColorLabel, ui_->SearchGamesPanelFilterFontColor)
            .row(ui_->SearchGamesPanelFieldFontColorLabel, ui_->SearchGamesPanelFieldFontColor)
            .row(ui_->SearchGamesPanelEditBoxFontColorLabel, ui_->SearchGamesPanelEditBoxFontColor)
            .row(ui_->SearchGamesPanelEditBoxBackgroundColorLabel, ui_->SearchGamesPanelEditBoxBackgroundColor);
        Group imageSplitters(tr("Image Splitters"), "ImageSplittersBox");
        imageSplitters.row(nullptr, ui_->ImageSplitterStyleSelector, {ui_->ImageSplitterStyleSelectorButtonDefault})
            .line({ui_->ImagetSplitterSingleColorTitleLabel, ui_->ImageSplitterSingleColorButtonDefault},
                  ui_->ImageSplitterShowGripIcon)
            .row(ui_->ImageSplitterSingleColorLabel, ui_->ImageSplitterSingleColor)
            .row(ui_->ImageSplitterSingleColorHotLabel, ui_->ImageSplitterSingleColorHot);
        Group border(tr("Image Border Color (7 Pixels)"), "ImageBorderColorBox");
        border.row(ui_->ImageBorderColorLabel, ui_->ImageBorderColor, {ui_->ImageBorderColorButtonDefault});
        Group docs(tr("Game Documents Colors"), "MAMEGameDocsBox");
        docs.row(ui_->GameDocumentsBackgroundColorLabel, ui_->GameDocumentsBackgroundColor, {ui_->GameDocumentsButtonDefault})
            .row(ui_->GameDocsBorderColorLabel, ui_->GameDocsBorderColor, {ui_->GameDocsBorderColorButtonDefault})
            .line({ui_->GameDocsShowStatusBar}, ui_->GameDocsFont_Setting);
        ui_->GameDocsFont_Setting->setMinimumSize(110, 30);
        ui_->GameDocsFont_Setting->setAlignment(Qt::AlignCenter);
        Group back(tr("Games List Background"), "GamesListBackgroundBox");
        back.row(ui_->GamesBackgroundColorLabel, ui_->GamesBackgroundColor, {ui_->GamesBackgroundColorButtonDefault})
            .line({ui_->GamesBackgroundImageEnable, ui_->GamesTileBackground})
            .row(nullptr, ui_->GamesBackgroundImage, {ui_->GamesBackgroundImageButtonSelect, ui_->GamesBackgroundImageButtonUpdate});
        Page p("PreferencesPage4", tr("The colors of the games list, the tool bar, the picture panels and the game documents."), 3);
        p.add(0, sel).add(0, bar).add(1, tool).add(1, hintBox).add(1, splitters).add(2, search).add(2, imageSplitters)
            .add(2, border).add(2, docs).add(2, back).finish();
        add(tr("Colors"), p.widget());
    }
    return out;
}

} // namespace eld
