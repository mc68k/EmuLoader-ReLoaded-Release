// MainWindow: is there a newer EmuLoader ReLoaded? Help > Check for Updates, and once a week a few
// seconds after EmuLoader starts (Settings > General > Check for Updates Once a Week, on by
// default). Only GitHub's latest release of the release repository is asked for (UpdateCheck,
// UpdateFetch); a newer version is offered with its page: EmuLoader never downloads or installs
// anything itself. EmuLoaderRL.ini [EmuLoaderRL]: CheckForUpdates, LastUpdateCheck (when it last
// had an answer, UTC) and SkipUpdateVersion (the version the user said to skip).
#include "MainWindow.h"

#include "DiagnosticLog.h"
#include "SplashScreen.h"
#include "UpdateCheck.h"
#include "UpdateFetch.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <memory>

namespace eld {

namespace {
constexpr qint64 kWeekSeconds = 7 * 24 * 3600;
const char* kReleasesPage = "https://github.com/mc68k/EmuLoader-ReLoaded-Release/releases";
QString shownVersion(const UpdateCheck::Release& r) {
    QString v = r.tag;
    if (v.startsWith('v') || v.startsWith('V')) v.remove(0, 1);
    return v;
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
    // the weekly check: a few seconds after the start, so it never holds it up
    QTimer::singleShot(5000, this, [this] { autoCheckForUpdates(); });
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
        // shown when nothing else is: not over the splash screen or another window's question
        // (each wait holds it; it holds itself only weakly, so it's gone once it has run)
        auto show = std::make_shared<std::function<void()>>();
        std::weak_ptr<std::function<void()>> self = show;
        *show = [this, manual, isNewer, answer, release, why, running, self] {
            if (StartupSplash::active() || !isVisible() || QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
                if (auto again = self.lock()) QTimer::singleShot(500, this, [again] { (*again)(); });
                return;
            }
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
            QMessageBox box(QMessageBox::Information, tr("Update Available"),
                            tr("%1 %2 is available. You have %3.").arg(product, shownVersion(release), running),
                            QMessageBox::NoButton, this);
            box.setObjectName("UpdateAvailable");
            QString info = tr("View Release opens its page, with what's new and the download. "
                              "EmuLoader doesn't download or install anything itself.");
            if (!manual) info += "\n\n" + tr("Turn Off stops the weekly check. It can be turned on again in Settings > General.");
            box.setInformativeText(info);
            QPushButton* view = box.addButton(tr("View Release"), QMessageBox::AcceptRole);
            QPushButton* skip = box.addButton(tr("Skip This Version"), QMessageBox::ActionRole);
            QPushButton* later = box.addButton(tr("Remind Me Later"), QMessageBox::RejectRole);
            QPushButton* off = manual ? nullptr : box.addButton(tr("Turn Off"), QMessageBox::DestructiveRole);
            box.setDefaultButton(view);
            box.setEscapeButton(later);
            box.exec();
            WinIni& ini = fe_.ini();
            if (box.clickedButton() == view) {
                QDesktopServices::openUrl(QUrl(release.page.isEmpty() ? QString::fromLatin1(kReleasesPage) : release.page));
            } else if (box.clickedButton() == skip) {
                ini.writeString("EmuLoaderRL", "SkipUpdateVersion", release.tag);
                fe_.saveIni();
            } else if (off && box.clickedButton() == off) {
                ini.writeInteger("EmuLoaderRL", "CheckForUpdates", 0);
                fe_.saveIni();
            }
        };
        (*show)();
    });
}

} // namespace eld
