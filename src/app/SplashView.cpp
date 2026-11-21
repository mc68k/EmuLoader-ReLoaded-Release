#include "SplashView.h"

#include "ElWidgets.h"
#include "SplashEffects.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace eld {

namespace {
// How long INSERT COIN keeps blinking after the effect, before the splash screen may close.
constexpr double kCoinHold = 1.3;
} // namespace

SplashView::SplashView(const QString& imagesFolder, const SplashSettings& s, QWidget* parent)
    : QWidget(parent), imagesFolder_(imagesFolder), s_(s), cache_(std::make_unique<SplashEffectCache>()) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    anim_.setInterval(16);
    connect(&anim_, &QTimer::timeout, this, &SplashView::stepAnimation);
    setScale(1.0);
}

SplashView::~SplashView() = default;

void SplashView::setSettings(const SplashSettings& s) {
    bool logo = s.alternateLogo != s_.alternateLogo;
    s_ = s;
    if (logo) artworkDpr_ = 0;
    invalidate();
}

void SplashView::setScale(double scale) {
    scale_ = scale;
    setFixedSize(int(std::round(ArtW * scale)), int(std::round(ArtH * scale)));
    artworkDpr_ = 0;
    invalidate();
}

QSize SplashView::sizeHint() const { return size(); }

void SplashView::setTitle(const QString& t) { if (t != title_) { title_ = t; invalidate(); } }
void SplashView::setMessage(const QString& m) { if (m != message_) { message_ = m; invalidate(); } }
void SplashView::setCount(const QString& c) { if (c != count_) { count_ = c; invalidate(); } }
void SplashView::setProgress(int percent) {
    percent = percent < 0 ? -1 : std::min(100, percent);
    if (percent != progress_) { progress_ = percent; invalidate(); }
}
void SplashView::setTimer(qint64 ms) {
    qint64 s = ms / 1000;
    QString t = QString::asprintf("%02lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    if (t != timer_) { timer_ = t; invalidate(); }
}
void SplashView::setVersionText(const QString& v) { if (v != version_) { version_ = v; invalidate(); } }
void SplashView::setEditable(bool on) { editable_ = on; setCursor(Qt::ArrowCursor); invalidate(); }

void SplashView::invalidate() {
    baseValid_ = false;
    update();
}

void SplashView::changeEvent(QEvent* e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    if (e->type() == QEvent::DevicePixelRatioChange) { // moved to a screen with another scaling
        artworkDpr_ = 0;
        invalidate();
    }
#endif
    QWidget::changeEvent(e);
}

// LoadImageLogo: logo.png / logo2.png, or the 4K pictures (logo-4K.png, logo-4K_extra.png) when
// the screen shows more pixels than the plain picture has.
void SplashView::loadArtwork() {
    const qreal dpr = devicePixelRatioF();
    if (artworkDpr_ == dpr * scale_ && !artwork_.isNull()) return;
    artworkDpr_ = dpr * scale_;
    const double need = ArtW * scale_ * dpr;
    QStringList names = need > 1960 * 1.05 ? QStringList{"logo-4K_extra.png", "logo-4K.png", "logo.png"}
                      : need > 980 * 1.05  ? QStringList{"logo-4K.png", "logo-4K_extra.png", "logo.png"}
                                           : QStringList{"logo.png"};
    if (s_.alternateLogo) { // logo2-4K.png ..., else logo2.png, else the usual picture
        QStringList alt;
        for (const QString& n : names) alt << QString(n).replace("logo", "logo2");
        alt.removeDuplicates();
        if (!alt.contains("logo2.png")) alt << "logo2.png";
        names = alt + names;
    }
    artwork_ = {};
    for (const QString& n : names) {
        QString f = QDir(imagesFolder_).filePath(n);
        if (QFileInfo::exists(f) && artwork_.load(f)) break;
    }
}

const QImage& SplashView::base() {
    if (baseValid_) return base_;
    loadArtwork();
    const qreal dpr = devicePixelRatioF() * scale_;
    QSize px(int(std::round(ArtW * dpr)), int(std::round(ArtH * dpr)));
    if (base_.size() != px) base_ = QImage(px, QImage::Format_RGB32);
    // painted scaled by hand, in artwork pixels: at a ratio of 1 while painting (a painter on an image
    // with a ratio scales by it too, which zoomed Ultra Size's picture by 1.5 twice and cut it off),
    // the ratio set after (checked by paintEvent)
    base_.setDevicePixelRatio(1.0);
    QPainter p(&base_);
    p.scale(px.width() / double(ArtW), px.height() / double(ArtH));
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(QRect(0, 0, ArtW, ArtH), QColor(0x27, 0x27, 0x27)); // the form's color (2565927)
    if (!artwork_.isNull()) p.drawPixmap(QRectF(0, 0, ArtW, ArtH), artwork_, artwork_.rect());
    drawTexts(p);
    p.end();
    base_.setDevicePixelRatio(dpr);
    baseValid_ = true;
    return base_;
}

void SplashView::drawShadowText(QPainter& p, const QRect& r, int flags, const QString& text, int which,
                                const QFont& f) const {
    if (text.isEmpty()) return;
    p.setFont(f);
    QString shown = QFontMetrics(f).elidedText(text, Qt::ElideRight, r.width());
    if (s_.shadows && s_.shadowDistance > 0) { // TShadowLabel: the shadow, offset down and right
        p.setPen(s_.shadowFor(which));
        p.drawText(r.translated(s_.shadowDistance, s_.shadowDistance), flags, shown);
    }
    p.setPen(ElColorBox::toQColor(s_.textColor[which]));
    p.drawText(r, flags, shown);
}

// The labels of TFormStatus, where uStatus.dfm puts them (StatusChangeLayout,
// SetLabelSoftwareScanCountVisible).
void SplashView::drawTexts(QPainter& p) const {
    QFont tahoma("Tahoma");
    tahoma.setBold(true);
    QFont title = tahoma, msg = tahoma, ver = versionFont();
    title.setPixelSize(13);
    msg.setPixelSize(12);
    QFont mono("Consolas"); // the original's Terminal font, or Consolas where Windows hasn't it
    mono.setStyleHint(QFont::Monospace);
    mono.setPixelSize(12);
    mono.setBold(true);
    const bool alt = s_.alternateLayout;
    const int vAlign = Qt::AlignLeft | Qt::AlignVCenter;
    // progress bar, timer and title
    drawProgress(p);
    drawShadowText(p, QRect(4, alt ? 548 : 566, 67, 16), vAlign, timer_, SplashSettings::Timer, mono);
    drawShadowText(p, QRect(alt ? 4 : 76, 563, 685 + (alt ? 72 : 0), 19), vAlign, title_, SplashSettings::Title, title);
    // the count, and the message after it
    int msgX = 4;
    if (!count_.isEmpty()) {
        int w = std::min(QFontMetrics(mono).horizontalAdvance(count_) + 2, 400);
        drawShadowText(p, QRect(4, 584, w, 16), vAlign, count_, SplashSettings::Count, mono);
        msgX = std::max(92, 4 + w + 9);
    }
    drawShadowText(p, QRect(msgX, 582, 761 - msgX + 200, 17), vAlign, message_, SplashSettings::Message, msg);
    // the version
    if (!version_.isEmpty()) {
        QRect vr = versionRect();
        // the version, and the build under it (smaller), both starting at the same left edge
        const int verH = QFontMetrics(ver).height() + 2;
        const QRect text = vr.adjusted(3, 0, 0, 0);
        drawShadowText(p, QRect(text.x(), vr.y(), text.width(), verH), Qt::AlignLeft | Qt::AlignVCenter, version_,
                       SplashSettings::Version, ver);
        drawShadowText(p, QRect(text.x(), vr.y() + verH, text.width(), vr.height() - verH), Qt::AlignLeft | Qt::AlignTop,
                       buildText(), SplashSettings::Version, buildFont());
        if (editable_) { // in Customize Splash Screen: a frame shows it can be dragged
            p.setPen(QPen(ElColorBox::toQColor(s_.textColor[SplashSettings::Version]), 1, Qt::DotLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(vr.adjusted(-3, -2, 2, 1));
        }
    }
}

// The version: Tahoma bold, a little larger than the original's 12 pixels.
QFont SplashView::versionFont() {
    QFont ver("Tahoma");
    ver.setBold(true);
    ver.setPixelSize(15);
    return ver;
}

// "Build: 927183e", under the version: the git revision the program was built from
QString SplashView::buildText() {
#ifndef EL_BUILD_REV
#define EL_BUILD_REV "dev"
#endif
    return QCoreApplication::translate("eld::SplashView", "Build: %1").arg(QStringLiteral(EL_BUILD_REV));
}

QFont SplashView::buildFont() {
    QFont f("Tahoma");
    f.setBold(true);
    f.setPixelSize(12);
    return f;
}

// The version and the build under it (dragged together in Customize Splash Screen)
QRect SplashView::versionRect() const {
    QFontMetrics fm(versionFont()), fb(buildFont());
    int w = std::max(fm.horizontalAdvance(version_), fb.horizontalAdvance(buildText())) + 6;
    int h = fm.height() + 2 + fb.height() + 1;
    int x = s_.versionX < 0 ? ArtW - w - 10 : std::clamp(s_.versionX, 0, ArtW - w); // -1: against the right edge
    int y = std::clamp(s_.versionY, 0, ArtH - h);
    return QRect(x, y, w, h);
}

// TXiProgressBar.Paint: the background gradient, the bar's gradient up to the position, a border.
void SplashView::drawProgress(QPainter& p) const {
    if (progress_ < 0) return;
    const ProgressColors c = s_.progressColors();
    QRect r = s_.alternateLayout ? QRect(76, 547, 64, 14) : QRect(4, 548, 64, 14);
    auto grad = [&](const QRect& area, int top, int bottom) {
        QLinearGradient g(0, area.top(), 0, area.bottom() + 1);
        g.setColorAt(0, ElColorBox::toQColor(top));
        g.setColorAt(1, ElColorBox::toQColor(bottom));
        p.fillRect(area, g);
    };
    QRect inner = r.adjusted(1, 1, -1, -1);
    grad(inner, c.backFace, c.backGrad);
    int pos = int(std::round((r.width() - 2) * progress_ / 100.0));
    if (pos > 0) grad(QRect(inner.left(), inner.top(), pos, inner.height()), c.foreFace, c.foreGrad);
    p.setPen(ElColorBox::toQColor(c.border));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r.adjusted(0, 0, -1, -1));
}

// ------------------------------------------------------------------ effects

void SplashView::play(const QString& effect, bool insertCoin) {
    effect_ = SplashSettings::effectKeys().contains(effect) ? effect : QString("none");
    insertCoin_ = insertCoin;
    cache_->reset();
    skippedAt_ = -1;
    finishedAt_ = -1;
    playing_ = true;
    effectT_ = 0;
    frameClock_.start();
    if (effect_ == "none") finishedAt_ = 0;
    anim_.start();
    lastPaint_.invalidate();
    update();
}

double SplashView::effectTime() const { return playing_ ? effectT_ : 1e9; }

void SplashView::seek(double t) {
    frozen_ = true;
    anim_.stop();
    effectT_ = t;
    if (effect_ != "none" && t < splashEffectDuration(effect_)) finishedAt_ = -1;
    else if (finishedAt_ < 0) finishedAt_ = splashEffectDuration(effect_);
}

void SplashView::advance() {
    if (!playing_ || frozen_) return;
    effectT_ += std::min(frameClock_.restart() / 1000.0, 0.05);
}

bool SplashView::effectRunning() const {
    return playing_ && effect_ != "none" && skippedAt_ < 0 && effectTime() < splashEffectDuration(effect_);
}

bool SplashView::busy() const {
    if (!playing_) return false;
    if (effectRunning()) return true;
    if (!insertCoin_ || skippedAt_ >= 0) return false;
    double end = finishedAt_ >= 0 ? finishedAt_ : splashEffectDuration(effect_);
    return effectTime() < end + kCoinHold;
}

void SplashView::skip() {
    if (!playing_ || skippedAt_ >= 0) return;
    bool running = effectRunning();
    skippedAt_ = effectTime();
    finishedAt_ = skippedAt_;
    if (running) emit effectFinished();
    emit skipped();
    update();
}

void SplashView::stepAnimation() {
    if (effect_ != "none" && finishedAt_ < 0 && !effectRunning()) {
        finishedAt_ = splashEffectDuration(effect_);
        emit effectFinished();
    }
    // keep animating while the effect plays and INSERT COIN blinks
    if (!effectRunning() && !insertCoin_) anim_.stop();
    update();
}

void SplashView::paintNow() {
    if (lastPaint_.isValid() && lastPaint_.elapsed() < 15) return;
    stepAnimation();
    repaint();
}

void SplashView::paintEvent(QPaintEvent*) {
    lastPaint_.start();
    if (baseValid_ && !qFuzzyCompare(base_.devicePixelRatio(), devicePixelRatioF() * scale_)) { // another screen's scaling
        artworkDpr_ = 0;
        baseValid_ = false;
    }
    advance();
    QPainter p(this);
    p.scale(scale_, scale_);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QImage& img = base();
    if (effectRunning()) {
        paintSplashEffect(p, effect_, effectTime(), img, *cache_);
    } else {
        paintAfterEffect(p, playing_ ? effect_ : QString("none"), img, *cache_);
        if (insertCoin_ && playing_) {
            double end = finishedAt_ >= 0 ? finishedAt_ : splashEffectDuration(effect_);
            paintInsertCoin(p, std::max(0.0, effectTime() - end));
        }
    }
}

// ------------------------------------------------------------------ mouse and keys

void SplashView::mousePressEvent(QMouseEvent* e) {
    if (effectRunning()) {
        skip();
        return;
    }
    if (editable_ && e->button() == Qt::LeftButton) {
        QPointF art = e->position() / scale_;
        if (versionRect().adjusted(-3, -2, 2, 1).contains(art.toPoint())) {
            dragging_ = true;
            dragOffset_ = art.toPoint() - versionRect().topLeft();
            setCursor(Qt::ClosedHandCursor);
            return;
        }
    }
    if (!editable_) emit closeRequested(); // after the effect: a click closes the splash screen
    QWidget::mousePressEvent(e);
}

void SplashView::mouseMoveEvent(QMouseEvent* e) {
    QPoint art = (e->position() / scale_).toPoint();
    if (dragging_) {
        QRect vr = versionRect();
        s_.versionX = std::clamp(art.x() - dragOffset_.x(), 0, ArtW - vr.width());
        s_.versionY = std::clamp(art.y() - dragOffset_.y(), 0, ArtH - vr.height());
        invalidate();
        emit versionMoved(s_.versionX, s_.versionY);
        return;
    }
    if (editable_) setCursor(versionRect().adjusted(-3, -2, 2, 1).contains(art) ? Qt::OpenHandCursor : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(e);
}

void SplashView::mouseReleaseEvent(QMouseEvent* e) {
    if (dragging_) {
        dragging_ = false;
        setCursor(Qt::OpenHandCursor);
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

void SplashView::keyPressEvent(QKeyEvent* e) {
    if (effectRunning()) {
        skip();
        return;
    }
    if (!editable_) emit closeRequested();
    QWidget::keyPressEvent(e);
}

} // namespace eld
