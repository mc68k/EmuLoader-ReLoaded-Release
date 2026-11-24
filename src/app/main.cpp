// EmuLoader (C++/Qt) - entry point.
//
// Data folder: like the Delphi EmuLoader, everything lives next to the program
// (EmuLoader.ini, arcade\games\, resources\). To use an existing EmuLoader
// installation, run with  --data "C:\Path\To\EmuLoader"  or put this program in
// that folder.
#include "UpdateInstall.h"
#include "FirstStart.h"
#include "SplashScreen.h"
#include "Frontend.h"
#include "Language.h"
#include "MainWindow.h"

#include "CrashLog.h"
#include "DiagnosticLog.h"

#include <QApplication>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QLoggingCategory>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QLockFile>
#include <QSaveFile>
#include <QCommandLineParser>
#include <QDir>
#include <QFileDialog>
#include <QPushButton>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

static void copyTree(const QString& from, const QString& to) {
    QDir src(from);
    QDir().mkpath(to);
    for (const QFileInfo& fi : src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString dst = QDir(to).filePath(fi.fileName());
        if (fi.isDir()) copyTree(fi.absoluteFilePath(), dst);
        else if (!QFileInfo::exists(dst)) QFile::copy(fi.absoluteFilePath(), dst);
    }
}

int main(int argc, char** argv) {
#ifdef Q_OS_WIN
    // No Windows message boxes of its own for a program that can't be started (a broken mame.exe:
    // "Unsupported 16-Bit Application") or a drive with no disk: EmuLoader says so itself
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
#endif
    QApplication app(argc, argv);
    eld::installCrashLog(QDir(QCoreApplication::applicationDirPath()).filePath("crash.log")); // until the data folder is known
    // The original's .ico files hold PNG pictures with extra bytes after the image; Qt reads them
    // fine, but libpng warns about each one ("IDAT: Extra compressed data").
    QLoggingCategory::setFilterRules("qt.gui.imageio=false");
    QApplication::setApplicationName("EmuLoader");
    QApplication::setOrganizationName("EmuLoader");
    QApplication::setStyle("Fusion");
#ifdef Q_OS_WIN
    QApplication::setStyle("windowsvista"); // the light theme uses native Windows controls
#endif

    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({"data", "EmuLoader data folder (an existing EmuLoader installation works).", "folder"});
    cli.addOption({"debug-log", "Write EmuLoaderRL\\diagnostic.log, whatever Preferences says."});
    // Install Update starts the new version with this, while the old one is still closing
    cli.addOption({"after-update", "Started by Install Update: waits for the version it replaced to close."});
    cli.process(app);

    QString appDir = QCoreApplication::applicationDirPath();
    eld::UpdateInstall::cleanUp(appDir); // (an update's download folder)
    QString data = cli.value("data");
    if (data.isEmpty()) {
        // Portable by default, like the original; fall back to a per-user folder if the
        // program folder isn't writable (e.g. installed under Program Files).
        data = appDir;
        if (!QFileInfo(appDir).isWritable())
            data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    QDir().mkpath(data);
    QDir().mkpath(QDir(data).filePath("EmuLoaderRL"));
    eld::installCrashLog(QDir(data).filePath("EmuLoaderRL/crash.log"));
    if (qEnvironmentVariable("EMULOADER_TEST_CRASH") == "1") { // checks crash.log
        volatile int* nowhere = nullptr;
        *nowhere = 1;
    }
    bool cleanInstall = !QFileInfo::exists(QDir(data).filePath("EmuLoaderRL/EmuLoaderRL.ini")); // this version's settings
    // First run in a new folder: copy the bundled data template (icons, images, filter files).
    QString tmpl = QDir(appDir).filePath("data_template");
    if (QFileInfo(tmpl).isDir() && !QFileInfo::exists(QDir(data).filePath("resources"))) copyTree(tmpl, data);

    eld::Frontend fe;
    if (!fe.open(data)) {
        QMessageBox::critical(nullptr, "EmuLoader", QCoreApplication::translate("main", "Cannot open data folder %1").arg(data));
        return 1;
    }
    // Preferences > Enable Diagnostic Log (or --debug-log): EmuLoaderRL\diagnostic.log
#ifndef EL_BUILD_REV
#define EL_BUILD_REV "dev"
#endif
#ifndef EL_PRODUCT_NAME
#define EL_PRODUCT_NAME "EmuLoader ReLoaded"
#define EL_VERSION_LABEL ""
#endif
    eld::DiagnosticLog::onFailure([](const QString& file, const QString& error) {
        QMetaObject::invokeMethod(qApp, [file, error] {
            QMessageBox::warning(nullptr, "EmuLoader", QCoreApplication::translate("main", "Diagnostic logging has stopped because the log could not be written.") +
                                                           "\n" + QDir::toNativeSeparators(file) + "\n" + error);
        }, Qt::QueuedConnection);
    });
    eld::DiagnosticLog::open(fe.userFile("diagnostic.log"),
                             QStringLiteral(EL_PRODUCT_NAME " " EL_VERSION_LABEL " (build " EL_BUILD_REV ")"),
                             fe.ini().readBool("Preferences", "DiagnosticLog", false), cli.isSet("debug-log"));
    // Language (lang\<Name>.ini next to the program or in the data folder). A clean install
    // asks for it when there's more than English; Settings > General changes it later.
    QStringList langDirs{QDir(appDir).filePath("lang"), QDir(data).filePath("lang")};
    QString lang = fe.ini().readString("EmuLoaderRL", "Language");
    if (cleanInstall && lang.isEmpty()) {
        auto langs = eld::LanguageFile::available(langDirs);
        lang = langs.size() > 1 ? eld::chooseLanguage(langs, eld::LanguageFile::systemDefault(langs)) : "English";
        if (lang.isEmpty()) lang = "English";
        fe.ini().writeString("EmuLoaderRL", "Language", lang);
        fe.saveIni();
    }
    QString used = eld::installLanguage(lang, langDirs, QDir(appDir).filePath("translations"));
    if (used != "English" && used != lang) { // found by its shown name: the file was renamed
        fe.ini().writeString("EmuLoaderRL", "Language", used);
        fe.saveIni();
    }
    // A clean install with no EmuLoader to bring data from in this folder: asked once whether
    // there's one elsewhere (FirstStart.cpp).
    if (cleanInstall && !fe.imported()) eld::askImportElsewhere(fe);
    // Preferences > Allow Only One Instance (on by default, as in the original): a second start
    // on the same data folder brings the running window to the front and exits.
    QLockFile lock(QDir(data).filePath("EmuLoaderRL.lock"));
    QString signal = QDir(data).filePath("EmuLoaderRL.activate");
    // (after an update: the version it replaced may still be closing, holding the lock)
    if (fe.ini().readBool("Preferences", "AllowOneInstance", true) && !lock.tryLock(cli.isSet("after-update") ? 30000 : 0)) {
        if (lock.error() == QLockFile::LockFailedError) {
#ifdef Q_OS_WIN
            AllowSetForegroundWindow(ASFW_ANY); // the running instance may take the focus
#endif
            QSaveFile f(signal);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(QByteArray::number(QDateTime::currentMSecsSinceEpoch()));
                f.commit();
            }
            return 0;
        }
    }
    // The splash screen (TFormStatus) while the main window is built and the games list read;
    // its effect plays to its end before the main window shows.
    eld::StartupSplash::show(fe);
    eld::MainWindow w(fe);
    eld::StartupSplash::finish([&w] {
        if (w.startMaximized()) w.showMaximized();
        else w.show();
        w.raise();
        w.activateWindow();
    });
    QFile::remove(signal);
    QFileSystemWatcher watch;
    watch.addPath(QDir(data).absolutePath());
    QObject::connect(&watch, &QFileSystemWatcher::directoryChanged, &w, [&w, signal] {
        if (!QFile::exists(signal)) return;
        QFile::remove(signal);
        if (w.isMinimized()) w.setWindowState(w.windowState() & ~Qt::WindowMinimized);
        w.show();
        w.raise();
        w.activateWindow();
    });
    const int code = app.exec();
    eld::DiagnosticLog::close();
    return code;
}
