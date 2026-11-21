#include "ProgressWindow.h"

#include "SplashSettings.h"
#include "SplashView.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

namespace eld {

namespace {
QString gImages, gIni;
} // namespace

void ProgressWindow::setLook(const QString& imagesFolder, const QString& splashIni) {
    gImages = imagesFolder;
    gIni = splashIni;
}

ProgressWindow::ProgressWindow(const QString& title, QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint) {
    setWindowTitle(title);
    setModal(true);
    SplashSettings s = gIni.isEmpty() ? SplashSettings() : SplashSettings::load(gIni);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    view_ = new SplashView(gImages, s, this);
    view_->setScale(s.largeSize ? 1.5 : 1.0);
    view_->setVersionText(QStringLiteral(EL_PRODUCT_NAME " " EL_VERSION_LABEL));
    view_->setTitle(title);
    lay->addWidget(view_);
    // Cancel, over the bottom right corner of the picture (the original had none)
    cancel_ = new QPushButton(tr("Cancel"), view_);
    cancel_->setAutoDefault(false);
    cancel_->setFocusPolicy(Qt::NoFocus);
    cancel_->setStyleSheet("QPushButton{color:white;background:rgba(0,0,0,150);border:1px solid rgba(255,255,255,120);"
                           "border-radius:3px;padding:2px 10px;}QPushButton:hover{background:rgba(60,60,60,190);}"
                           "QPushButton:disabled{color:rgba(255,255,255,90);border-color:rgba(255,255,255,50);}");
    cancel_->adjustSize();
    cancel_->move(view_->width() - cancel_->width() - 8, view_->height() - cancel_->height() - 8);
    connect(cancel_, &QPushButton::clicked, this, [this] { canceled_ = true; cancel_->setEnabled(false); });
    // fits on the screen
    QScreen* sc = parent && parent->isVisible() ? parent->screen() : QGuiApplication::primaryScreen();
    if (sc) {
        QRect avail = sc->availableGeometry();
        if (view_->width() > avail.width() || view_->height() > avail.height()) {
            view_->setScale(view_->scale() * std::min(avail.width() / double(view_->width()), avail.height() / double(view_->height())) * 0.95);
            cancel_->move(view_->width() - cancel_->width() - 8, view_->height() - cancel_->height() - 8);
        }
        QRect over = parent && parent->isVisible() ? parent->frameGeometry() : avail;
        adjustSize();
        move(over.center() - QPoint(view_->width() / 2, view_->height() / 2));
    }
    clock_.start();
    lastPaint_.start();
    show();
    refresh(true);
}

void ProgressWindow::step(const QString& text, int total) {
    view_->setMessage(text);
    view_->setCount({});
    total_ = std::max(0, total);
    view_->setProgress(total_ > 1 ? 0 : -1); // UpdateProgressBar: shown once there's a total
    refresh(true);
}

void ProgressWindow::setProgress(int done, const QString& countText) {
    if (total_ > 1) view_->setProgress(std::min(99, int(qint64(done) * 100 / total_))); // never "done" before it is
    if (!countText.isNull()) view_->setCount(countText);
    refresh();
}

void ProgressWindow::setCancelable(bool on) {
    cancel_->setVisible(true);
    cancel_->setEnabled(on && !canceled_);
    refresh(true);
}

void ProgressWindow::reject() {
    if (cancel_->isEnabled()) cancel_->click();
}

// The elapsed time, and a chance for the window to paint and see a click on Cancel (at most
// ten times a second, so a fast job isn't slowed down by it).
void ProgressWindow::refresh(bool force) {
    if (!force && lastPaint_.elapsed() < 100) return;
    lastPaint_.restart();
    view_->setTimer(clock_.elapsed());
    QApplication::processEvents(QEventLoop::AllEvents, 20);
}

} // namespace eld
