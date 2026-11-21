// MainWindow: Settings - every settings window of the original as a tab of one window
// (SettingsDialog). Each tab is the window it was, with its own code and ini keys; what the
// main window did after it closed with OK runs after Settings closes with OK.
#include "MainWindow.h"

#include "DiagnosticLog.h"
#include "SettingsSearch.h"
#include "PreferencesDialog.h"
#include "SettingsDialog.h"
#include "SplashCustomizeDialog.h"
#include "SplashScreen.h"

#include <QHBoxLayout>
#include <QHash>
#include <QLineEdit>
#include <QTabWidget>

namespace eld {

void MainWindow::openSettings(const QString& tab, const QString& reveal) {
    SettingsDialog s(this);
    AfterSettings after;
    QHash<QString, int> index; // the English titles, for tab
    auto mark = [&](const QString& english) { index.insert(english, s.tabs()->count()); };

    // Preferences: General, Games List, Game Info and Documents, Colors (its controls)
    const LightMode lightBefore = lightMode_;
    auto* prefs = new PreferencesDialog(fe_, &s);
    prefs->hide();
    connect(prefs, &PreferencesDialog::lightModeChanged, this, [this](const LightMode& lm) { if (!night_) applyLightMode(lm); });
    connect(prefs, &PreferencesDialog::iconsFolderChanged, this, [this] {
        if (!mamu_.options().enabled) return;
        mamu_.setFolders(fe_.iconFolders());
        refreshIcons();
    });
    const char* prefTitles[] = {"General", "Games List", "Game Info and Documents", "Colors"};
    const auto prefPages = prefs->takePages();
    for (int i = 0; i < prefPages.size(); ++i) {
        mark(i < 4 ? prefTitles[i] : prefPages[i].first);
        if (i == 0)
            s.addPage(prefPages[i].first, prefPages[i].second, [prefs] { static_cast<QDialog*>(prefs)->accept(); return true; },
                      [this, prefs, lightBefore] {
                          prefs->done(QDialog::Rejected); // (its reject() saves: Cancel doesn't)
                          if (!night_) applyLightMode(lightBefore);
                      });
        else
            s.addPage(prefPages[i].first, prefPages[i].second);
    }
    after << [this] {
        DiagnosticLog::setPreference(fe_.ini().readBool("Preferences", "DiagnosticLog", false));
        applyPreferences();
        findManual(); // the manuals folder may have changed
    };

    mark("Night Mode");
    s.addLazyPage(tr("Night Mode"), [this, &s, &after] { addNightModePage(s, after); });
    mark("Pictures");
    s.addLazyPage(tr("Pictures"), [this, &s, &after] { addPicturesPage(s, after); });
    mark("Layouts");
    s.addLazyPage(tr("Layouts"), [this, &s, &after] { addLayoutsPage(s, after); });
    mark("Thumbnails");
    s.addLazyPage(tr("Thumbnails"), [this, &s, &after] { addThumbnailsPage(s, after); });
    mark("Video");
    s.addLazyPage(tr("Video"), [this, &s, &after] { addVideoPage(s, after); });
    mark("Emulators");
    s.addLazyPage(tr("Emulators"), [this, &s, &after] { addEmulatorsPage(s, after); });
    mark("Tool Bar");
    s.addLazyPage(tr("Tool Bar"), [this, &s, &after] { addToolBarPage(s, after); });
    mark("Splash Screen");
    s.addLazyPage(tr("Splash Screen"), [this, &s] {
        s.addDialog(tr("Splash Screen"), new SplashCustomizeDialog(StartupSplash::imagesFolder(fe_), StartupSplash::settingsFile(fe_), &s),
                    "", "", {"QDialogButtonBox"});
    });

    // The order: setting up (General, Emulators), what's shown of each game, then how EmuLoader
    // looks. (The tabs are made in the order above: the Preferences window's four come together.)
    {
        static const QStringList order = {"General", "Emulators", "Games List", "Pictures", "Layouts", "Thumbnails",
                                          "Video", "Game Info and Documents", "Tool Bar", "Colors", "Night Mode",
                                          "Splash Screen"};
        QList<int> from;
        for (const QString& t : order)
            if (index.contains(t)) from << index.value(t);
        for (int i = 0; i < s.tabs()->count(); ++i)
            if (!from.contains(i)) from << i; // (any other at the end)
        s.arrange(from);
        QHash<QString, int> now;
        for (auto it = index.cbegin(); it != index.cend(); ++it) now.insert(it.key(), int(from.indexOf(it.value())));
        index = now;
    }

    // Find a setting (at the right of the tabs): the tab, and the setting highlighted
    {
        // (in a box of its own: the tabs leave a corner its size hint wide, and a line edit's is
        // narrower than its minimum width)
        auto* box = new QWidget(&s);
        auto* find = new QLineEdit(box);
        find->setObjectName("FindSetting");
        find->setPlaceholderText(tr("Find a setting..."));
        find->setToolTip(tr("Type part of a setting's name and press Enter"));
        find->setClearButtonEnabled(true);
        find->setMinimumWidth(fontMetrics().averageCharWidth() * 28);
        auto* l = new QHBoxLayout(box);
        l->setContentsMargins(4, 0, 2, 2);
        l->addWidget(find);
        s.tabs()->setCornerWidget(box, Qt::TopRightCorner);
        connect(find, &QLineEdit::returnPressed, &s, [&s, prefs, find] {
            QList<SettingEntry> entries;
            prefs->addSettingsTo(entries);
            FindSettingDialog dlg(entries, find->text(), &s);
            if (dlg.exec() == QDialog::Accepted)
                if (QWidget* w = prefs->reveal(dlg.chosen().target)) s.showPageOf(w);
        });
    }
    // F1: the guide's section for the tab
    static const QHash<QString, QString> anchors = {
        {"General", "prefs-general"}, {"Games List", "prefs-list"}, {"Game Info and Documents", "docs"}, {"Colors", "prefs-colors"},
        {"Night Mode", "night"}, {"Pictures", "imagetools"}, {"Layouts", "layouts"}, {"Thumbnails", "list"},
        {"Video", "video"}, {"Emulators", "emusetup"}, {"Tool Bar", "toolbar"}, {"Splash Screen", "splash"}};
    for (auto it = index.cbegin(); it != index.cend(); ++it)
        if (QWidget* page = s.tabs()->widget(it.value())) page->setProperty("helpAnchor", anchors.value(it.key(), "settingswin"));
    s.setCurrentPage(index.value(tab, 0));
    if (!reveal.isEmpty())
        if (QWidget* w = prefs->reveal(reveal)) s.showPageOf(w);
    DiagnosticLog::write("preferences", "Settings opened" + (tab.isEmpty() ? QString() : " at " + tab));
    s.widenForTabs();      // (all the tabs' names in a row)
    s.buildRestWhenIdle(); // (the other tabs, while the window is open)
    const bool ok = s.exec() == QDialog::Accepted;
    // (an earlier build remembered the window's size)
    fe_.ini().deleteKey("EmuLoaderRL", "SettingsWidth");
    fe_.ini().deleteKey("EmuLoaderRL", "SettingsHeight");
    // the log as just set (General > Enable Diagnostic Log), so turning it on logs this already
    DiagnosticLog::setPreference(fe_.ini().readBool("Preferences", "DiagnosticLog", false));
    DiagnosticLog::write("preferences", ok ? "Settings closed with OK" : "Settings canceled");
    if (ok) {
        for (const auto& f : after) f();
        // saved now, as the original's settings were (its ini file was written at once), not only
        // when EmuLoader closes: a crash or a shutdown doesn't lose them
        fe_.saveIni();
    }
}

} // namespace eld
