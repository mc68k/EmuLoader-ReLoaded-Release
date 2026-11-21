#include "SplashScreen.h"

#include "Frontend.h"
#include "SplashView.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QPointer>
#include <QRandomGenerator>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>
#include <thread>

namespace eld {

SplashWindow::SplashWindow(const QString& imagesFolder, const SplashSettings& s, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint) {
    setWindowTitle(QStringLiteral(EL_PRODUCT_NAME));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    view_ = new SplashView(imagesFolder, s, this);
    view_->setScale(s.largeSize ? 1.5 : 1.0); // UltraSize_4Kx2: half as big again
    view_->setVersionText(QStringLiteral(EL_PRODUCT_NAME " " EL_VERSION_LABEL));
    lay->addWidget(view_);
    setFixedSize(view_->size());
}

void SplashWindow::centerOn(QWidget* w) {
    QScreen* sc = w && w->isVisible() ? w->screen() : QGuiApplication::primaryScreen();
    QRect area = w && w->isVisible() ? w->frameGeometry() : (sc ? sc->availableGeometry() : QRect(0, 0, 1280, 720));
    QSize sz = size();
    if (sc) { // too big for the screen: smaller
        QRect avail = sc->availableGeometry();
        if (sz.width() > avail.width() || sz.height() > avail.height()) {
            double k = std::min(avail.width() / double(sz.width()), avail.height() / double(sz.height())) * 0.95;
            view_->setScale(view_->scale() * k);
            setFixedSize(view_->size());
            sz = size();
        }
        area = area.intersected(avail).isEmpty() ? avail : area;
    }
    move(area.center() - QPoint(sz.width() / 2, sz.height() / 2));
}

// ------------------------------------------------------------------ startup

namespace StartupSplash {

namespace {
QPointer<SplashWindow> gSplash;
QElapsedTimer gShown;
constexpr int kMinimumMs = 1900; // so that it doesn't only flash by
constexpr int kLookMs = 1000;    // after the effect (and INSERT COIN): a moment to look at it
} // namespace

QString settingsFile(const Frontend& fe) { return fe.iniFilesFolder() + "splash.ini"; }
QString imagesFolder(const Frontend& fe) { return fe.resourcesFolder() + "images"; }

void show(Frontend& fe) {
    SplashSettings s = SplashSettings::load(settingsFile(fe));
    QString effect = s.effect;
    if (effect == "random") { // a different one each start
        QStringList keys = SplashSettings::effectKeys();
        if (keys.size() > 1) keys.removeAll(s.lastRandom);
        effect = keys.at(int(QRandomGenerator::global()->bounded(keys.size())));
        s.lastRandom = effect;
        s.save(settingsFile(fe));
    }
    gSplash = new SplashWindow(imagesFolder(fe), s);
    gSplash->setAttribute(Qt::WA_DeleteOnClose);
    gSplash->setWindowFlag(Qt::WindowStaysOnTopHint);
    gSplash->centerOn(nullptr);
    gSplash->view()->setTitle(QCoreApplication::translate("SplashScreen", "Initializing"));
    gSplash->show();
    gSplash->raise();
    gSplash->view()->play(effect, s.insertCoin);
    gShown.start();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

// (closed but not deleted yet counts as gone: the main window is up)
bool active() { return gSplash && gSplash->isVisible(); }

void status(const QString& title, const QString& message) {
    if (!gSplash) return;
    if (!title.isNull()) gSplash->view()->setTitle(title);
    if (!message.isNull()) gSplash->view()->setMessage(message);
    gSplash->view()->paintNow();
}

void pump() {
    if (gSplash) gSplash->view()->paintNow();
}

void runWhileShowing(const std::function<void()>& work) {
    if (!gSplash || !gSplash->view()->busy()) {
        work();
        return;
    }
    std::atomic<bool> done{false};
    std::thread t([&] {
        work();
        done = true;
    });
    while (!done) {
        pump();
        QThread::msleep(5);
    }
    t.join();
}

void finish(const std::function<void()>& showMain) {
    if (!gSplash) {
        if (showMain) showMain();
        return;
    }
    // the effect (and INSERT COIN after it) plays to its end, then the splash screen stays a
    // second more; a click or a key skips the effect, a second one closes it
    QEventLoop loop;
    QTimer check;
    QElapsedTimer done; // since the effect ended
    check.setInterval(20);
    QObject::connect(&check, &QTimer::timeout, &loop, [&] {
        if (!gSplash) return loop.quit();
        if (gSplash->view()->busy()) return done.invalidate();
        if (!done.isValid()) done.start();
        if (done.elapsed() >= kLookMs && gShown.elapsed() >= kMinimumMs) loop.quit();
    });
    QObject::connect(gSplash->view(), &SplashView::closeRequested, &loop, &QEventLoop::quit);
    check.start();
    loop.exec(); // the main window isn't shown yet: clicks and keys go to the splash screen
    if (showMain) showMain();
    if (gSplash) gSplash->close();
}

} // namespace StartupSplash

} // namespace eld
