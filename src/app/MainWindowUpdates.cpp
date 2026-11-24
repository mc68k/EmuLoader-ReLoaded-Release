// MainWindow: is there a newer EmuLoader ReLoaded? Help > Check for Updates, and once a week a few
// seconds after EmuLoader starts (Settings > General > Check for Updates Once a Week, on by
// default). Only GitHub's latest release of the release repository is asked for (UpdateCheck,
// UpdateFetch). A newer version is offered with what's new, and two ways to get it:
//   Install Update      EmuLoader downloads the release's Windows zip (UpdateDownload), checks
//                       it, puts its files in the program folder (UpdateInstall: the files it
//                       replaces go to previous-version) and restarts on the new version
//   Download It Myself  the release's page, with the zip
// EmuLoaderRL.ini [EmuLoaderRL]: CheckForUpdates, LastUpdateCheck (when it last had an answer,
// UTC) and SkipUpdateVersion (the version the user said to skip).
#include "MainWindow.h"

#include "DiagnosticLog.h"
#include "SplashScreen.h"
#include "UpdateCheck.h"
#include "UpdateDownload.h"
#include "UpdateFetch.h"
#include "UpdateInstall.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QTextBrowser>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>
#include <memory>

namespace eld {

namespace {
constexpr qint64 kWeekSeconds = 7 * 24 * 3600;
const char* kReleasesPage = "https://github.com/mc68k/EmuLoader-ReLoaded-Release/releases";
QString shownVersion(const QString& tag) {
    QString v = tag;
    if (v.startsWith('v') || v.startsWith('V')) v.remove(0, 1);
    return v;
}
// Install Update is for the Windows version, from a release that has its zip
bool canInstall(const UpdateCheck::Release& r) {
#ifdef Q_OS_WIN
    return !r.zip.url.isEmpty();
#else
    return !r.zip.url.isEmpty() && qEnvironmentVariableIsSet("EMULOADER_UPDATE_URL"); // (an answer from a file)
#endif
}
} // namespace

void MainWindow::setupUpdateCheck() {
    // Help > Check for Updates..., after the guide
    if (QAction* before = menus_.action("MenuHelpViewQuickSetupGuide")) {
        auto* check = new QAction(tr("Check for Updates..."), before->parent());
        check->setObjectName("MenuCheckForUpdates");
        check->setToolTip(tr("Asks GitHub whether there's a newer version of EmuLoader ReLoaded"));
        connect(check, &QAction::triggered, this, [this] { checkForUpdates(true); });
        if (auto* m = qobject_cast<QMenu*>(before->parent())) m->insertAction(before, check);
    }
    // started by Install Update: says so
    if (QCoreApplication::arguments().contains("--after-update")) {
        DiagnosticLog::write("update", "Started after an update: " EL_VERSION_LABEL);
        whenNothingShown([this] {
            QMessageBox box(QMessageBox::Information, tr("Update Installed"),
                            tr("EmuLoader ReLoaded has been updated to %1.").arg(QStringLiteral(EL_VERSION_LABEL)),
                            QMessageBox::Ok, this);
            box.setObjectName("UpdateInstalled");
            box.setInformativeText(tr("The version it replaced is kept in the previous-version folder, "
                                      "in case you want to go back to it."));
            box.exec();
        });
    }
    // the weekly check: a few seconds after the start, so it never holds it up
    QTimer::singleShot(5000, this, [this] { autoCheckForUpdates(); });
}

void MainWindow::whenNothingShown(std::function<void()> fn) {
    // (each wait holds it; it holds itself only weakly, so it's gone once it has run)
    auto run = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> self = run;
    *run = [this, fn, self] {
        if (StartupSplash::active() || !isVisible() || QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
            if (auto again = self.lock()) QTimer::singleShot(500, this, [again] { (*again)(); });
            return;
        }
        fn();
    };
    (*run)();
}

void MainWindow::autoCheckForUpdates() {
    WinIni& ini = fe_.ini();
    if (!ini.readBool("EmuLoaderRL", "CheckForUpdates", true)) return;
    const QDateTime last = QDateTime::fromString(ini.readString("EmuLoaderRL", "LastUpdateCheck"), Qt::ISODate);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    // (a date in the future: the clock was changed; checked again)
    if (last.isValid() && last <= now && last.secsTo(now) < kWeekSeconds) return;
    checkForUpdates(false);
}

void MainWindow::checkForUpdates(bool manual) {
    if (updateChecking_) return;
    updateChecking_ = true;
    const QString url = UpdateCheck::url();
    DiagnosticLog::write("update", QString("Checking for updates%1: %2").arg(manual ? " (Help menu)" : "").arg(url));
    fetchUpdateInfo(url, this, [this, manual](const FetchResult& r) {
        updateChecking_ = false;
        const QString running = QStringLiteral(EL_VERSION_LABEL);
        UpdateCheck::Release release;
        QString why;
        UpdateCheck::Answer answer = UpdateCheck::Answer::Unreadable;
        if (r.status == 404) answer = UpdateCheck::Answer::NoneYet;
        else if (r.status == 200) answer = UpdateCheck::parse(r.body, &release, &why);
        else if (r.status == 403 || r.status == 429) why = tr("GitHub is limiting how often it can be asked; try again in an hour");
        else if (r.status != 0) why = tr("GitHub answered with an error (%1)").arg(r.status);
        else why = r.error;
        DiagnosticLog::write("update", QString("Update check: status %1, %2").arg(r.status)
                                           .arg(answer == UpdateCheck::Answer::Release ? "latest " + release.tag
                                                : answer == UpdateCheck::Answer::NoneYet ? QString("no release yet")
                                                                                         : "no answer: " + why));
        WinIni& ini = fe_.ini();
        if (answer != UpdateCheck::Answer::Unreadable) { // an answer: the next weekly check a week from now
            ini.writeString("EmuLoaderRL", "LastUpdateCheck", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
            fe_.saveIni();
        }
        const bool isNewer = answer == UpdateCheck::Answer::Release && UpdateCheck::newer(release.tag, running);
        if (!manual) {
            // the weekly check speaks up only about a newer version, and not one the user skipped
            if (!isNewer || release.tag.compare(ini.readString("EmuLoaderRL", "SkipUpdateVersion"), Qt::CaseInsensitive) == 0) return;
        }
        whenNothingShown([this, manual, isNewer, answer, release, why, running] {
            const QString product = QStringLiteral(EL_PRODUCT_NAME);
            if (!isNewer) {
                if (answer == UpdateCheck::Answer::Release)
                    QMessageBox::information(this, tr("Check for Updates"),
                                             tr("You have the latest version: %1 %2.").arg(product, running));
                else if (answer == UpdateCheck::Answer::NoneYet)
                    QMessageBox::information(this, tr("Check for Updates"),
                                             tr("No release of %1 has been published yet. You have %2.").arg(product, running));
                else
                    QMessageBox::warning(this, tr("Check for Updates"),
                                         tr("EmuLoader couldn't check for updates: %1.").arg(why) + "\n\n" +
                                             tr("Check the internet connection and try again later."));
                return;
            }
            // the offer: the versions, what's new, and the ways to get it
            const bool install = canInstall(release);
            QDialog box(this);
            box.setObjectName("UpdateAvailable");
            box.setWindowTitle(tr("Update Available"));
            auto* lay = new QVBoxLayout(&box);
            auto* head = new QLabel(tr("%1 %2 is available. You have %3.").arg(product, shownVersion(release.tag), running));
            QFont hf = head->font();
            hf.setBold(true);
            hf.setPointSizeF(hf.pointSizeF() * 1.15);
            head->setFont(hf);
            lay->addWidget(head);
            if (!release.notes.trimmed().isEmpty()) {
                lay->addWidget(new QLabel(tr("What's new:")));
                auto* notes = new QTextBrowser;
                notes->setObjectName("UpdateNotes");
                notes->setOpenExternalLinks(true);
                notes->setMarkdown(release.notes);
                notes->setMinimumSize(560, 240);
                lay->addWidget(notes, 1);
            }
            QString info = install ? tr("Install Update downloads the new version and installs it, then restarts EmuLoader. "
                                        "Your settings, games lists and favorites stay as they are, and this version is kept "
                                        "in the previous-version folder.") + "\n\n" +
                                         tr("Download It Myself opens the release's page, with the zip.")
                                   : tr("Download It Myself opens the release's page, with the download.");
            if (!manual) info += "\n\n" + tr("Turn Off stops the weekly check. It can be turned on again in Settings > General.");
            auto* infoLabel = new QLabel(info);
            infoLabel->setWordWrap(true);
            lay->addWidget(infoLabel);
            auto* buttons = new QDialogButtonBox;
            enum Choice { None, Install, Page, Skip, Later, Off };
            auto choice = std::make_shared<Choice>(Later);
            auto add = [&](const QString& text, const char* name, Choice c, QDialogButtonBox::ButtonRole role) {
                QPushButton* b = buttons->addButton(text, role);
                b->setObjectName(name);
                connect(b, &QPushButton::clicked, &box, [&box, choice, c] {
                    *choice = c;
                    box.accept();
                });
                return b;
            };
            QPushButton* first = nullptr;
            if (install) first = add(tr("Install Update"), "UpdateInstall", Install, QDialogButtonBox::AcceptRole);
            QPushButton* page = add(tr("Download It Myself"), "UpdateDownloadMyself", Page, QDialogButtonBox::AcceptRole);
            add(tr("Skip This Version"), "UpdateSkip", Skip, QDialogButtonBox::ActionRole);
            add(tr("Remind Me Later"), "UpdateLater", Later, QDialogButtonBox::RejectRole);
            if (!manual) add(tr("Turn Off"), "UpdateTurnOff", Off, QDialogButtonBox::DestructiveRole);
            (first ? first : page)->setDefault(true);
            lay->addWidget(buttons);
            box.exec(); // (Esc or the window's X: Remind Me Later)
            WinIni& ini = fe_.ini();
            switch (*choice) {
            case Install: installUpdate(release); break;
            case Page:
                QDesktopServices::openUrl(QUrl(release.page.isEmpty() ? QString::fromLatin1(kReleasesPage) : release.page));
                break;
            case Skip:
                ini.writeString("EmuLoaderRL", "SkipUpdateVersion", release.tag);
                fe_.saveIni();
                break;
            case Off:
                ini.writeInteger("EmuLoaderRL", "CheckForUpdates", 0);
                fe_.saveIni();
                break;
            default: break;
            }
        });
    });
}

void MainWindow::installUpdate(const UpdateCheck::Release& release) {
    const QString programDir = QCoreApplication::applicationDirPath();
    const QString version = shownVersion(release.tag);
    // when it can't: why, and the release's page instead
    auto cantInstall = [this, release](const QString& why) {
        DiagnosticLog::write("update", "Install Update failed: " + why);
        QMessageBox box(QMessageBox::Warning, tr("Install Update"), tr("The update couldn't be installed: %1.").arg(why),
                        QMessageBox::NoButton, this);
        box.setObjectName("UpdateFailed");
        box.setInformativeText(tr("Nothing was changed. You can download the update yourself from its page."));
        QPushButton* page = box.addButton(tr("Download It Myself"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(page);
        box.exec();
        if (box.clickedButton() == page)
            QDesktopServices::openUrl(QUrl(release.page.isEmpty() ? QString::fromLatin1(kReleasesPage) : release.page));
    };
    if (!UpdateInstall::canWrite(programDir)) {
        cantInstall(tr("EmuLoader isn't allowed to change the files in its folder (%1)").arg(QDir::toNativeSeparators(programDir)));
        return;
    }
    const QString work = UpdateInstall::workFolder(programDir);
    QDir(work).removeRecursively();
    QDir().mkpath(work);
    const QString zip = QDir(work).filePath(release.zip.name.isEmpty() ? QStringLiteral("update.zip") : release.zip.name);
    DiagnosticLog::write("update", "Install Update: downloading " + release.zip.url);

    auto* progress = new QProgressDialog(tr("Downloading %1 %2...").arg(QStringLiteral(EL_PRODUCT_NAME), version), tr("Cancel"), 0,
                                         release.zip.size > 0 ? 1000 : 0, this);
    progress->setObjectName("UpdateDownloading");
    progress->setWindowTitle(tr("Install Update"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setValue(0);
    progress->show();
    auto stop = std::make_shared<std::atomic_bool>(false);
    connect(progress, &QProgressDialog::canceled, this, [stop] { *stop = true; });
    const QLocale loc;
    downloadUpdate(
        release.zip.url, zip, stop, progress,
        [progress, loc, release](DownloadProgress p) {
            const qint64 total = p.total > 0 ? p.total : release.zip.size;
            if (total > 0) {
                progress->setMaximum(1000);
                progress->setValue(int(qMin<qint64>(1000, p.received * 1000 / total)));
                progress->setLabelText(tr("Downloading %1 %2... %3 of %4")
                                           .arg(QStringLiteral(EL_PRODUCT_NAME), shownVersion(release.tag),
                                                loc.formattedDataSize(p.received), loc.formattedDataSize(total)));
            }
        },
        [this, progress, stop, release, zip, work, programDir, version, cantInstall](const QString& error) {
            progress->deleteLater();
            if (*stop) { // Cancel: nothing changed, nothing to say
                DiagnosticLog::write("update", "Install Update: download stopped");
                QDir(work).removeRecursively();
                return;
            }
            if (!error.isEmpty()) {
                QDir(work).removeRecursively();
                cantInstall(tr("the download failed (%1)").arg(error));
                return;
            }
            const QString bad = UpdateInstall::verify(zip, release.zip.size, release.zip.sha256);
            if (!bad.isEmpty()) {
                QDir(work).removeRecursively();
                cantInstall(bad);
                return;
            }
            // unpacked and put in place on another thread (half a minute on some computers: the
            // window keeps showing how far it is)
            auto* busy = new QProgressDialog(tr("Installing %1 %2...").arg(QStringLiteral(EL_PRODUCT_NAME), version), QString(), 0, 0, this);
            busy->setObjectName("UpdateInstalling");
            busy->setWindowTitle(tr("Install Update"));
            busy->setWindowModality(Qt::WindowModal);
            busy->setMinimumDuration(0);
            busy->setAutoClose(false);
            busy->setAutoReset(false);
            busy->setWindowFlag(Qt::WindowCloseButtonHint, false);
            busy->show();
            const QString unpacked = QDir(work).filePath("files");
            const QString about =
                tr("EmuLoader ReLoaded %1 (build %2): the version that was replaced by %3 on %4.")
                    .arg(QStringLiteral(EL_VERSION_LABEL), QStringLiteral(EL_BUILD_REV), version,
                         QLocale().toString(QDate::currentDate(), QLocale::LongFormat)) +
                "\n\n" + tr("To go back to it, close EmuLoader and copy these files over the ones in the program folder.") + "\n";
            QPointer<QProgressDialog> shown(busy);
            QPointer<MainWindow> self(this);
            QThreadPool::globalInstance()->start([=] {
                QString why;
                auto tell = [shown, version](int done, int total) {
                    QMetaObject::invokeMethod(shown, [shown, version, done, total] {
                        if (!shown) return;
                        shown->setMaximum(total);
                        shown->setValue(done);
                        shown->setLabelText(tr("Installing %1 %2... unpacking file %3 of %4")
                                                .arg(QStringLiteral(EL_PRODUCT_NAME), version).arg(done).arg(total));
                    }, Qt::QueuedConnection);
                };
                const bool ok = UpdateInstall::unpack(zip, unpacked, &why, tell) &&
                                UpdateInstall::install(unpacked, programDir, UpdateInstall::previousFolder(programDir), about, &why);
                QMetaObject::invokeMethod(qApp, [self, shown, ok, why, work, version, cantInstall, programDir] {
                    if (shown) shown->deleteLater();
                    if (!self) return;
                    if (!ok) {
                        QDir(work).removeRecursively();
                        cantInstall(why);
                        return;
                    }
                    self->restartAfterUpdate(version, programDir);
                }, Qt::QueuedConnection);
            });
        });
}

void MainWindow::restartAfterUpdate(const QString& version, const QString& programDir) {
    DiagnosticLog::write("update", "Install Update: " + version + " installed; restarting");
    // the new version starts once this one has closed (it waits for it: --after-update)
    QStringList args = QCoreApplication::arguments().mid(1);
    args.removeAll("--after-update");
    args << "--after-update";
    const QString program = QDir(programDir).filePath(QFileInfo(QCoreApplication::applicationFilePath()).fileName());
    connect(qApp, &QCoreApplication::aboutToQuit, qApp, [program, args, programDir] {
        QProcess::startDetached(program, args, programDir);
    });
    close();
    QTimer::singleShot(0, qApp, [] { QApplication::quit(); });
}

} // namespace eld
