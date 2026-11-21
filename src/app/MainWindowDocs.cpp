// MainWindow - the MAME game documents panel: port of TFormMain.ToggleGameDocuments,
// ShowGameInfoAll, SetSinglePanelLayoutGameDocuments, UpdateScreenshotsPanel, the
// Images / Docs tab buttons and the PopupGameDocuments menu. Settings: el_extras.ini
// [GameDocuments] (on/off, layout, sizes, order, display mode, tab) and
// ini_files\lightmode.ini [GameDocuments] (colors, font, status bar).
#include "MainWindow.h"
#include "ScriptFonts.h"
#include "DiagnosticLog.h"
#include "ElWidgets.h"
#include "GameInfoPage.h"
#include "PlayedGames.h"
#include "ZipArchive.h"
#include "PreferencesDialog.h"
#include "VclRuntime.h"
#include "WinIni.h"
#include "ui_FormMain.h"

#include <QActionGroup>
#include <QDesktopServices>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QFontDialog>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShortcut>
#include <QTextBrowser>
#include <QUrl>

namespace eld {

static const char* kDocs = "GameDocuments";

QString MainWindow::extrasIni() const { return fe_.userFile("el_extras.ini"); }

void MainWindow::readDocsSettings() {
    WinIni ini;
    ini.load(extrasIni());
    docsSize_[0] = ini.readInteger(kDocs, "LayoutTop_Height", 151);
    docsSize_[1] = ini.readInteger(kDocs, "LayoutBottom_Height", 151);
    docsSize_[2] = ini.readInteger(kDocs, "LayoutLeft_Width", 151);
    docsSize_[3] = ini.readInteger(kDocs, "LayoutRight_Width", 151);
    docsLayout_ = std::clamp(ini.readInteger(kDocs, "LayoutIndex", 0), 0, 3);
    docsOn_ = ini.readBool(kDocs, "AutoGameDocs", false);
    docsOrder_ = GameDocs::readOrder(ini);
    docsTabs_ = ini.readInteger(kDocs, "DisplayMode", 0) == 1;
    docsTab_ = std::clamp(ini.readInteger(kDocs, "DisplayModeTabIndex", 0), 0, 1);
}

// UpdateExtraIni: the main window's part (the order and display mode are written by Preferences).
void MainWindow::saveDocsSettings() {
    WinIni ini;
    ini.load(extrasIni());
    ini.writeInteger(kDocs, "LayoutTop_Height", docsSize_[0]);
    ini.writeInteger(kDocs, "LayoutBottom_Height", docsSize_[1]);
    ini.writeInteger(kDocs, "LayoutLeft_Width", docsSize_[2]);
    ini.writeInteger(kDocs, "LayoutRight_Width", docsSize_[3]);
    ini.writeInteger(kDocs, "LayoutIndex", docsLayout_);
    ini.writeInteger(kDocs, "AutoGameDocs", docsOn_ ? 1 : 0);
    for (int i = 0; i < docsOrder_.size(); ++i)
        ini.writeString(kDocs, QString("DocIndex%1").arg(i + 1), QString("%1%2").arg(docsOrder_[i].first).arg(docsOrder_[i].second ? 1 : 0));
    ini.writeInteger(kDocs, "DisplayMode", docsTabs_ ? 1 : 0);
    ini.writeInteger(kDocs, "DisplayModeTabIndex", docsTab_);
    ini.save(extrasIni());
}

void MainWindow::setupGameDocs() {
    readDocsSettings();
    QTextBrowser* text = ui_->MAMEDocsText;
    text->setOpenLinks(false);
    text->setReadOnly(true);
    text->setUndoRedoEnabled(false);
    connect(text, &QTextBrowser::anchorClicked, this, [](const QUrl& u) { QDesktopServices::openUrl(u); });
    // Right-click: PopupGameDocuments
    text->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(text, &QWidget::customContextMenuRequested, this, [this, text](const QPoint& p) {
        if (QMenu* m = menus_.menu("PopupGameDocuments")) m->exec(text->viewport()->mapToGlobal(p));
    });
    // Show MAME Game Documents (games list menu, F3)
    if (QAction* a = menus_.action("PopupAutomaticGameInformation")) {
        a->setCheckable(true);
        a->setChecked(docsOn_);
        a->setShortcut(Qt::Key_F3);
        a->setShortcutContext(Qt::WindowShortcut);
        addAction(a);
        menus_.on("PopupAutomaticGameInformation", [this, a] { setDocsEnabled(a->isChecked()); });
    }
    menus_.on("PopupDisableGameDocs", [this] {
        setDocsEnabled(false);
        if (list_) gamesView()->setFocus();
    });
    menus_.on("GameDocumentsFont", [this] {
        bool ok = false;
        QFont f = QFontDialog::getFont(&ok, lightMode_.docsFont(), this, tr("Game Documents Font"));
        if (!ok) return;
        lightMode_.setDocsFont(f);
        lightMode_.save(fe_.iniFilesFolder() + "lightmode.ini");
        applyDocsLook();
    });
    menus_.on("GameDocumentsCustomizePanel", [this] { openSettings({}, "Setting_MAMEGameDocsBox"); });
    // Top / Bottom / Left / Right (HelpContext 0..3)
    auto* group = new QActionGroup(this);
    const char* layouts[] = {"GameDocumentsLayout1_Top", "GameDocumentsLayout2_Bottom", "GameDocumentsLayout3_Left",
                             "GameDocumentsLayout4_Right"};
    for (int i = 0; i < 4; ++i)
        if (QAction* a = menus_.action(layouts[i])) {
            a->setCheckable(true);
            a->setChecked(i == docsLayout_);
            group->addAction(a);
            menus_.on(layouts[i], [this, i] {
                docsLayout_ = i;
                applyDocsLayout();
            });
        }
    // Go To ...: visible for the documents the game has.
    const std::pair<const char*, int> gotos[] = {{"GameDocsGoToInformation", GameDocs::Info}, {"GameDocsGoToHistory", GameDocs::History},
                                                 {"GameDocsGoToDriverInfo", GameDocs::Driver}, {"GameDocsGoToStory", GameDocs::Story},
                                                 {"GameDocsGoToMarp", GameDocs::Marp}, {"GameDocsGoToGameInit", GameDocs::GameInit}};
    for (auto [name, doc] : gotos) {
        int d = doc;
        menus_.on(name, [this, d] { ui_->MAMEDocsText->scrollToAnchor(QString("doc%1").arg(d)); });
        if (QAction* a = menus_.action(name)) a->setVisible(false);
    }
    // Images / Docs tabs (Display Mode: Tabbed Panels)
    for (ElButton* b : {ui_->ButtonImagePanelToggle, ui_->ButtonDocsPanelToggle}) {
        b->setCheckable(true);
        b->setEnabled(true);
        b->setToolTip(b == ui_->ButtonImagePanelToggle ? tr("Images") : tr("Game documents"));
        int tab = b == ui_->ButtonDocsPanelToggle ? 1 : 0;
        disconnect(b, &QPushButton::clicked, nullptr, nullptr);
        connect(b, &QPushButton::clicked, this, [this, tab] {
            docsTab_ = tab;
            applyDocsLayout();
            updateImage();
            updateDocs();
        });
    }
    ui_->SplitterMAMEInfo->setCursor(Qt::SplitVCursor);
    connect(ui_->SplitterMAMEInfo, &ElSplitter::moved, this, [this] {
        QWidget* p = ui_->PanelGameDocuments;
        docsSize_[docsLayout_] = docsLayout_ < 2 ? p->height() : p->width();
    });
    applyDocsLook();
    if (docsOn_) reloadDocs();
    applyDocsLayout();
}

// ToggleGameDocuments(True): read the DAT files from ui.ini's historypath.
void MainWindow::reloadDocs() {
    docs_.load(GameDocs::datFolders(fe_.dataDir(), fe_.mameFolder(sys_)));
}

void MainWindow::setDocsEnabled(bool on) {
    docsOn_ = on;
    if (QAction* a = menus_.action("PopupAutomaticGameInformation")) a->setChecked(on);
    if (on) reloadDocs();
    else docs_.clear();
    applyDocsLayout();
    updateImage();
    updateDocs();
}

// SetSinglePanelLayoutGameDocuments + UpdateScreenshotsPanel + the tab buttons.
void MainWindow::applyDocsLayout() {
    QWidget* area = ui_->PanelImagesDocuments;
    QWidget* docs = ui_->PanelGameDocuments;
    QWidget* split = ui_->SplitterMAMEInfo;
    QWidget* image = ui_->PanelImage;
    if (QAction* a = menus_.action("PopupDisableGameDocs")) a->setVisible(docsOn_);
    for (ElButton* b : {ui_->ButtonImagePanelToggle, ui_->ButtonDocsPanelToggle}) b->setVisible(docsTabs_);
    ui_->ButtonImagePanelToggle->setChecked(docsTab_ == 0);
    ui_->ButtonDocsPanelToggle->setChecked(docsTab_ == 1);
    layoutImageToolBar();
    // GameDocsDisplayTabsToggleButtons: the image buttons do nothing on the Docs tab.
    bool imagesTab = !docsTabs_ || docsTab_ == 0;
    for (ElButton* b : {ui_->ButtonPreviousImage, ui_->ButtonNextImage, ui_->ButtonPreviousCategory, ui_->ButtonImageCategory,
                        ui_->ButtonNextCategory, ui_->ButtonPreviousLayout, ui_->ButtonScreenshotLayouts, ui_->ButtonNextLayout}) {
        if (!b->property("elDocsWasEnabled").isValid()) b->setProperty("elDocsWasEnabled", b->isEnabled());
        if (imagesTab) {
            b->setEnabled(b->property("elDocsWasEnabled").toBool());
            b->setProperty("elDocsWasEnabled", QVariant());
        } else {
            b->setEnabled(false);
        }
    }
    auto align = [](QWidget* w, const char* a) { w->setProperty("vclAlign", a); };
    bool showDocs, showImage, together = false;
    if (docsTabs_) {
        showImage = docsTab_ == 0;
        showDocs = docsOn_ && docsTab_ == 1;
    } else {
        showDocs = docsOn_;
        showImage = !docsOn_ || showImages_; // images off and docs on: the docs fill the panel
        together = showDocs && showImage;
    }
    image->setVisible(showImage);
    docs->setVisible(showDocs);
    split->setVisible(together);
    align(image, "alClient");
    if (!together) {
        align(docs, "alClient");
        align(split, "alNone");
    } else {
        // VCL aligns top / bottom / left / right controls in the order of their position, so the
        // panel goes on the outside and the splitter next to it.
        QRect r = area->rect();
        int size = std::max(40, docsSize_[docsLayout_]);
        static const char* sides[] = {"alTop", "alBottom", "alLeft", "alRight"};
        align(docs, sides[docsLayout_]);
        align(split, sides[docsLayout_]);
        switch (docsLayout_) {
        case 0:
            docs->setGeometry(0, 0, r.width(), size);
            split->setGeometry(0, size, r.width(), 7);
            break;
        case 1:
            docs->setGeometry(0, r.height() - size, r.width(), size);
            split->setGeometry(0, r.height() - size - 7, r.width(), 7);
            break;
        case 2:
            docs->setGeometry(0, 0, size, r.height());
            split->setGeometry(size, 0, 7, r.height());
            break;
        default:
            docs->setGeometry(r.width() - size, 0, size, r.height());
            split->setGeometry(r.width() - size - 7, 0, 7, r.height());
            break;
        }
        split->setCursor(docsLayout_ < 2 ? Qt::SplitVCursor : Qt::SplitHCursor);
    }
    vcl::relayout(area);
    vcl::relayout(docs);
    vcl::relayout(image);
}

// lightmode.ini [GameDocuments]: background, font and its color, border, status bar.
void MainWindow::applyDocsLook() {
    const LightMode& lm = lightMode_;
    QTextBrowser* text = ui_->MAMEDocsText;
    text->setFont(ScriptFonts::shown(lm.docsFont()));
    text->setStyleSheet(QString("QTextBrowser { background: %1; color: %2; border: 1px solid %3; }")
                            .arg(LightMode::qc(lm.docsBack).name(), LightMode::qc(lm.docsFontColor).name(),
                                 LightMode::qc(lm.docsBorder).name()));
    ui_->PanelGameDocuments->setProperty("vclColor", LightMode::qc(lm.docsBorder).name());
    ui_->MAMEInfoStatusBar->setVisible(lm.docsStatusBar);
    ui_->MAMEInfoStatusBar->setProperty("vclStyle", lm.statusGradient ? "vgSimple" : "vgSolid");
    ui_->MAMEInfoStatusBar->setProperty("vclColor1", LightMode::qc(lm.statusTop).name());
    ui_->MAMEInfoStatusBar->setProperty("vclColor2", LightMode::qc(lm.statusBottom).name());
    ui_->MAMEInfoStatusBar->update();
    vcl::relayout(ui_->PanelGameDocuments);
    updateDocs();
}

// The URLs in a line as links.
static QString linkify(const QString& escaped) {
    static const QRegularExpression url(R"((https?://[^\s<>"\]\)]+))");
    QString out = escaped;
    out.replace(url, R"(<a href="\1">\1</a>)");
    return out;
}

// ShowGameInfoAll: the documents of the selected game, in the order of Preferences.
void MainWindow::updateDocs() {
    QTextBrowser* text = ui_->MAMEDocsText;
    const std::pair<const char*, int> gotos[] = {{"GameDocsGoToInformation", GameDocs::Info}, {"GameDocsGoToHistory", GameDocs::History},
                                                 {"GameDocsGoToDriverInfo", GameDocs::Driver}, {"GameDocsGoToStory", GameDocs::Story},
                                                 {"GameDocsGoToMarp", GameDocs::Marp}, {"GameDocsGoToGameInit", GameDocs::GameInit}};
    for (auto [name, doc] : gotos)
        if (QAction* a = menus_.action(name)) a->setVisible(false);
    if (!docsOn_ || !ui_->PanelGameDocuments->isVisibleTo(this)) {
        if (!docsOn_) text->clear();
        return;
    }
    const MameGame* g = current();
    QString html;
    if (!g) { // ValidateSelectedGame: nothing without a game
        text->clear();
        return;
    }
    // a heading and what to do, in the middle of the panel, for when there's nothing to show
    auto notice = [](const QString& heading, const QStringList& paragraphs) {
        QString h = "<p></p><p></p><p align=\"center\" style=\"font-size: large;\"><b>" + heading.toHtmlEscaped() + "</b></p>";
        for (const QString& p : paragraphs) h += "<p align=\"center\">" + p + "</p>";
        return h;
    };
    if (!docs_.anyLoaded()) {
        // where they were looked for, and how to change it
        QStringList where;
        for (const QString& d : GameDocs::datFolders(fe_.dataDir(), fe_.mameFolder(sys_)))
            where << "<b>" + QDir::toNativeSeparators(d).toHtmlEscaped() + "</b>";
        html = notice(tr("No game documents were found."),
                      {tr("Game documents are files such as history.xml, mameinfo.dat, story.dat and gameinit.dat, "
                          "from their own websites.").toHtmlEscaped(),
                       (where.size() == 1 ? tr("EmuLoader looked for them in this folder:") : tr("EmuLoader looked for them in these folders:"))
                               .toHtmlEscaped() + "<br>" + where.join("<br>"),
                       tr("Put the files there, or set another folder with historypath in MAME's ui.ini (Menu Options > "
                          "MAME/Arcade > Emulators Default Settings).").toHtmlEscaped()});
    } else {
        QList<int> order;
        for (const auto& [doc, on] : docsOrder_)
            if (on) order << doc;
        static const char* titles[] = {nullptr, QT_TR_NOOP("Information Data"), QT_TR_NOOP("History Data"), QT_TR_NOOP("Driver Data"),
                                       QT_TR_NOOP("Story Data"), QT_TR_NOOP("MAME Action Replay Page"), QT_TR_NOOP("Game Initialization")};
        bool first = true;
        // a software list game's documents are found by "<list>\\<name>" (history.xml's <item list= name=>)
        const QString parent = g->isSoftware() && !g->cloneOf.isEmpty() ? g->softwareList + '\\' + g->cloneOf : g->cloneOf;
        for (const GameDocs::Section& s : docs_.sections(g->key(), parent, g->driver, order)) {
            if (!first) html += "<p></p>";
            first = false;
            html += QString("<a name=\"doc%1\"></a><p align=\"center\"><b> ---------- %2 ---------- </b></p>")
                        .arg(s.doc)
                        .arg(tr(titles[s.doc]).toHtmlEscaped());
            if (!s.version.isEmpty()) html += "<p>" + linkify(s.version.toHtmlEscaped()) + "</p><p></p>";
            QStringList lines;
            for (const QString& l : s.lines) lines << linkify(l.toHtmlEscaped());
            html += "<p style=\"white-space: pre-wrap;\">" + lines.join("<br>") + "</p>";
            for (auto [name, doc] : gotos)
                if (doc == s.doc)
                    if (QAction* a = menus_.action(name)) a->setVisible(true);
        }
        if (first) { // nothing about this game: say so, and which documents were looked in
            QStringList files;
            for (const QString& f : docs_.loadedFiles()) files << QFileInfo(f).fileName();
            const QString game = g->title.isEmpty() ? g->name : QString("%1 (%2)").arg(g->title, g->name);
            html = order.isEmpty()
                       ? notice(tr("Every game document is turned off."),
                                {tr("Turn them on in Settings > Game Info and Documents.").toHtmlEscaped()})
                       : notice(tr("Nothing about this game."),
                                {tr("The game documents (%1) have nothing about %2.").arg(files.join(", "), game).toHtmlEscaped()});
        }
    }
    text->setHtml(html);
    text->verticalScrollBar()->setValue(0);
}

} // namespace eld

namespace eld {

// Internet Game Info (ButtonInternetGameInfoClick): the selected game's page, in the default
// browser - the local page made from your files (GameInfoPage), or the website in Preferences
// (InternetGameInfoLink: %s the game; InternetGameSoftwareListInfoLink: %s the game, %s its list).
QString MainWindow::internetGameInfoUrl(const MameGame& g) {
    WinIni& ini = fe_.ini();
    if (ini.readInteger("Preferences", "InternetGameInfoLocal", 1) == 0) {
        QString link = g.isSoftware()
                           ? ini.readString("Preferences", "InternetGameSoftwareListInfoLink", "http://www.progettoemma.net/mess/gioco.php?game=%s&list=%s")
                           : ini.readString("Preferences", "InternetGameInfoLink", "http://www.progettoemma.net/gioco.php?game=%s");
        // Format(link, [GameName, SoftwareName]): each %s in turn
        const QStringList values{g.name, g.softwareList};
        int at = 0;
        for (const QString& v : values) {
            at = int(link.indexOf(QLatin1String("%s"), at));
            if (at < 0) break;
            const QString enc = QString::fromLatin1(QUrl::toPercentEncoding(v));
            link.replace(at, 2, enc);
            at += int(enc.size());
        }
        return link;
    }
    const QString folder = fe_.userFile("gameinfo");
    GameInfoPage::Input in;
    in.game = g;
    in.system = Frontend::section(sys_);
    in.emulator = fe_.emulator(sys_).version;
    in.generator = QStringLiteral(EL_PRODUCT_NAME " " EL_VERSION_LABEL " (build " EL_BUILD_REV ")");
    in.lastPlayed = g.lastPlayed ? PlayedGames::dateText(g.lastPlayed, false) : QString();
    in.playtime = g.playtime > 0 ? PlayedGames::playtimeText(g.playtime, false) : QString();
    for (const MameGame& o : db_.games()) {
        if (o.softwareList != g.softwareList) continue;
        if (!g.cloneOf.isEmpty() && o.name == g.cloneOf) in.parentTitle = o.title;
        if (g.cloneOf.isEmpty() && o.cloneOf == g.name) in.clones << o;
    }
    // the pictures of every category but the icons; a zipped one copied out to gameinfo\images
    for (int c = 0; c < ImageFinder::Count; ++c) {
        if (c == ImageFinder::Icons) continue;
        bool parents = false;
        ImageFinder::Found f = imageFinder_.find(c, g.imageName(), {}, 1);
        if (!f.ok && !g.cloneOf.isEmpty()) {
            f = imageFinder_.find(c, g.imageParent(), {}, 1);
            parents = f.ok;
        }
        if (!f.ok) continue;
        QString file = f.file;
        if (f.zipped()) {
            file = QDir(folder).filePath(QString("images/%1_%2.%3")
                                             .arg(QString(g.imageName()).replace('/', '_'), QString(ImageFinder::category(c).folder),
                                                  QFileInfo(f.entry).suffix().toLower()));
            ZipArchive za;
            const QByteArray data = za.open(f.file) ? za.read(f.entry) : QByteArray();
            QDir().mkpath(QFileInfo(file).absolutePath());
            QFile out(file);
            if (data.isEmpty() || !out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(data) != data.size()) continue;
        }
        in.pictures.append({QCoreApplication::translate("ImageCategory", ImageFinder::category(c).title), file, parents});
    }
    // the game documents, in the order and with the ones chosen for the Game Docs panel
    if (!docs_.anyLoaded()) docs_.load(GameDocs::datFolders(fe_.dataDir(), fe_.mameFolder(sys_)));
    QList<int> order;
    for (const auto& [doc, on] : docsOrder_)
        if (on) order << doc;
    static const char* titles[] = {nullptr, QT_TR_NOOP("Information Data"), QT_TR_NOOP("History Data"), QT_TR_NOOP("Driver Data"),
                                   QT_TR_NOOP("Story Data"), QT_TR_NOOP("MAME Action Replay Page"), QT_TR_NOOP("Game Initialization")};
    const QString parent = g.isSoftware() && !g.cloneOf.isEmpty() ? g.softwareList + '\\' + g.cloneOf : g.cloneOf;
    for (const GameDocs::Section& s : docs_.sections(g.key(), parent, g.driver, order))
        in.docs.append({tr(titles[s.doc]), s.version, s.lines});
    const QString file = GameInfoPage::file(folder, g);
    if (!GameInfoPage::write(file, in)) return {};
    return QUrl::fromLocalFile(file).toString();
}

void MainWindow::openInternetGameInfo() {
    const MameGame* g = current();
    if (!g) return;
    const QString url = internetGameInfoUrl(*g);
    const QString title = tr("Internet Game Info");
    if (url.isEmpty()) {
        QMessageBox::warning(this, title, tr("The game's page could not be written in:\n%1").arg(QDir::toNativeSeparators(fe_.userFile("gameinfo"))));
        return;
    }
    DiagnosticLog::write("process", "Internet Game Info: " + url);
    if (!QDesktopServices::openUrl(QUrl(url)))
        QMessageBox::warning(this, title, tr("No web browser opened the page. Choose a default browser in Windows, then try again.\n\n%1").arg(url));
}

} // namespace eld
