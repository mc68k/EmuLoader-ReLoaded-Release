// SplashView - the splash screen's picture (TFormStatus.ImageBk and its labels): the artwork
// (resources\images\logo.png, or logo2.png), the job's title, the step's message, the timer, the
// count, the version, and the progress bar, drawn over the artwork's bottom edge as the original
// does. At startup it can play one of the arcade effects first (SplashEffects.cpp), and
// "INSERT COIN" can blink after it.
//
// Everything is placed in the artwork's own pixels (980 x 600) and drawn at the view's scale;
// the sharper 4K artwork is used when the screen has the pixels for it.
#pragma once

#include "SplashSettings.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPixmap>
#include <QTimer>
#include <QWidget>
#include <memory>

namespace eld {

struct SplashEffectCache;

class SplashView : public QWidget {
    Q_OBJECT
public:
    static constexpr int ArtW = 980, ArtH = 600;

    SplashView(const QString& imagesFolder, const SplashSettings& s, QWidget* parent = nullptr);
    ~SplashView() override;

    void setSettings(const SplashSettings& s);
    const SplashSettings& settings() const { return s_; }
    void setScale(double scale);        // widget pixels per artwork pixel
    double scale() const { return scale_; }
    QSize sizeHint() const override;

    // TitleStr, MessageStr, the count label ("" hides it), the progress bar (-1 hides it), the timer
    void setTitle(const QString& t);
    void setMessage(const QString& m);
    void setCount(const QString& c);
    void setProgress(int percent);
    void setTimer(qint64 ms);
    void setVersionText(const QString& v);

    // Customize Splash Screen: the version text can be dragged (with a frame around it).
    void setEditable(bool on);
    QRect versionRect() const;          // in artwork pixels
    static QString buildText();         // "Build: <git revision>", under the version
    static QFont buildFont();
    static QFont versionFont();

    // Effects ("none", or a key of SplashSettings::effectKeys()); a click or a key skips it.
    void play(const QString& effect, bool insertCoin);
    void skip();
    bool effectRunning() const;
    bool busy() const;                  // an effect playing, or INSERT COIN blinking for a moment after it
    QString playing() const { return effect_; }
    // Draws now, for a caller that keeps the event loop waiting (startup): at most every 15 ms.
    void paintNow();
    // The effect stopped at a time (seconds).
    void seek(double t);

signals:
    void effectFinished();
    void versionMoved(int x, int y);
    void skipped();
    void closeRequested(); // a click or a key once the effect has ended

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void changeEvent(QEvent* e) override;

private:
    void loadArtwork();
    void invalidate();                  // the texts or the look changed: compose again
    const QImage& base();               // artwork + texts, at the device's resolution
    void drawTexts(QPainter& p) const;
    void drawProgress(QPainter& p) const;
    void drawShadowText(QPainter& p, const QRect& r, int flags, const QString& text, int which, const QFont& f) const;
    double effectTime() const;
    void stepAnimation();

    QString imagesFolder_;
    SplashSettings s_;
    double scale_ = 1.0;
    QPixmap artwork_;
    qreal artworkDpr_ = 0;
    QString title_, message_, count_, timer_ = "00:00:00", version_;
    int progress_ = -1;
    bool editable_ = false;
    bool dragging_ = false;
    QPoint dragOffset_;
    QImage base_;
    bool baseValid_ = false;

    QString effect_ = "none";
    bool insertCoin_ = false;
    // The effect's own clock: it moves on with each frame drawn, by the time since the last one,
    // but by no more than 50 ms, so that a moment when the program is busy (building the main
    // window) pauses the effect instead of skipping part of it.
    bool playing_ = false, frozen_ = false;
    double effectT_ = 0;
    QElapsedTimer frameClock_;
    void advance();
    double skippedAt_ = -1;             // the effect's time when it was skipped
    double finishedAt_ = -1;            // when the effect ended (for the blinking after it)
    QTimer anim_;
    QElapsedTimer lastPaint_;
    std::unique_ptr<SplashEffectCache> cache_;
};

} // namespace eld
