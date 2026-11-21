// MainWindow: what's missing before EmuLoader can show any games, said at start-up (every start,
// as nothing works without it): no emulator set up, the emulator's file gone (a moved folder, a
// new drive letter), or no games list yet. Each has a button that does what's needed; setting up
// the emulator goes on to creating the games list (addEmulatorsPage asks with askCreateGamesList).
#include "MainWindow.h"

#include "DiagnosticLog.h"
#include "SplashScreen.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <memory>

namespace eld {

namespace {
// The heading in bold, the explanation under it wide enough to read in a few lines
void shape(QMessageBox& box) {
    box.setTextFormat(Qt::RichText);
    box.setText("<b>" + box.text().toHtmlEscaped() + "</b>");
    box.setStyleSheet("QLabel#qt_msgbox_informativelabel { min-width: 380px; }");
}
} // namespace

// Settings > Emulators, from a Set Up MAME... button: the file field and its Select button flash
// three times, so it's plain where to start
void MainWindow::openEmulatorSetup() {
    // once the Settings window is open (however long it takes: a slow computer, a large screen
    // scaling), up to 10 seconds
    auto* wait = new QTimer(this);
    auto tries = std::make_shared<int>(0);
    QObject::connect(wait, &QTimer::timeout, this, [wait, tries] {
        QWidget* w = QApplication::activeModalWidget();
        QList<QWidget*> show;
        for (const char* n : {"Arcade_exec", "ButtonBrowseArcade_exec"})
            if (QWidget* c = w ? w->findChild<QWidget*>(n) : nullptr; c && c->isVisible()) show << c;
        if (show.isEmpty()) {
            if (++*tries >= 100) { // (gives up)
                wait->stop();
                wait->deleteLater();
            }
            return;
        }
        wait->stop();
        wait->deleteLater();
        if (auto* b = w->findChild<QWidget*>("ButtonBrowseArcade_exec")) b->setFocus();
        auto step = std::make_shared<int>(0);
        auto* t = new QTimer(w);
        QStringList before; // (their own looks, Night Mode's ..., put back after each flash)
        for (QWidget* c : show) before << c->styleSheet();
        QObject::connect(t, &QTimer::timeout, w, [show, before, step, t] {
            const bool on = *step % 2 == 0;
            for (int i = 0; i < show.size(); ++i) {
                QWidget* c = show[i];
                c->setProperty("setupFlash", on);
                c->setStyleSheet(on ? before[i] + QString(" %1 { background: #FFD966; color: black; }").arg(c->metaObject()->className())
                                    : before[i]);
            }
            if (++*step >= 6) t->deleteLater(); // three times, then as it was
        });
        t->start(300);
    });
    wait->start(100);
    openSettings("Emulators");
}

void MainWindow::startupSetupCheck() {
    if (StartupSplash::active() || !isVisible()) { // after the splash screen, over the main window
        QTimer::singleShot(100, this, [this] { startupSetupCheck(); });
        return;
    }
    setupPending_ = false;
    SetupAsking asking(this); // (the list's own message after the answer: Not Now)
    const QString name = Frontend::section(sys_);
    const QString saved = fe_.emulator(sys_).file;
    const bool anySet = fe_.emulator(SystemId::MAME).isSet() || fe_.emulator(SystemId::HBMAME).isSet();
    QString heading, text;
    if (!anySet) {
        heading = tr("No emulator is set up yet.");
        text = tr("EmuLoader runs your games with MAME. Tell it where mame.exe is, and it can then build the games list from it.");
    } else if (saved.isEmpty() || !QFileInfo::exists(fe_.emulatorFile(sys_))) {
        heading = tr("%1 isn't where EmuLoader expects it.").arg(name);
        text = tr("EmuLoader looked for %1 and couldn't find it. If %2 has moved, set its new place.")
                   .arg(QDir::toNativeSeparators(fe_.emulatorFile(sys_)), name);
    } else {
        if (db_.games().isEmpty() && !QFileInfo::exists(fe_.databaseFile(sys_))) askCreateGamesList(false);
        return;
    }
    QMessageBox box(QMessageBox::Information, QStringLiteral(EL_PRODUCT_NAME), heading, QMessageBox::NoButton, this);
    box.setObjectName("SetupCheckMessage");
    box.setInformativeText(text);
    shape(box);
    QPushButton* setUp = box.addButton(tr("Set Up %1...").arg(anySet ? name : QStringLiteral("MAME")), QMessageBox::AcceptRole);
    setUp->setObjectName("SetupCheckSetUp");
    box.addButton(tr("Not Now"), QMessageBox::RejectRole);
    box.setDefaultButton(setUp);
    DiagnosticLog::write("startup", "setup check: " + heading);
    box.exec();
    if (box.clickedButton() == setUp) openEmulatorSetup(); // then asks to create the games list
}

// afterSetup: the emulator was just set up (Emulators Setup); otherwise at start-up
void MainWindow::askCreateGamesList(bool afterSetup) {
    SetupAsking asking(this);
    const QString name = Frontend::section(sys_);
    QMessageBox box(QMessageBox::Question, afterSetup ? tr("Emulators Setup") : QStringLiteral(EL_PRODUCT_NAME),
                    afterSetup ? tr("%1 is set up.").arg(fe_.emulator(sys_).version) : tr("There's no games list yet."),
                    QMessageBox::NoButton, this);
    box.setObjectName("CreateGamesListMessage");
    box.setInformativeText(tr("EmuLoader builds its list of games from %1. It takes a minute or two.").arg(name));
    shape(box);
    QPushButton* create = box.addButton(tr("Create the Games List"), QMessageBox::AcceptRole);
    create->setObjectName("CreateGamesListNow");
    box.addButton(tr("Not Now"), QMessageBox::RejectRole);
    box.setDefaultButton(create);
    box.exec();
    if (box.clickedButton() == create) createGamesList();
}

} // namespace eld
