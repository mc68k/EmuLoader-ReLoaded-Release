#include "SplashEffects.h"

#include <QFontDatabase>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QTransform>
#include <algorithm>
#include <cmath>

namespace eld {

namespace {

constexpr double W = 980, H = 600;
const QRectF kFull(0, 0, W, H);

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
double easeOut(double t) { return 1 - std::pow(1 - clamp01(t), 3); }
double smooth(double t) { t = clamp01(t); return t * t * (3 - 2 * t); }
double phase(double t, double from, double to) { return clamp01((t - from) / (to - from)); }

// ------------------------------------------------------------------ vector grid

void grid(QPainter& p, double t, const QImage& base) {
    const double horizon = H * 0.52;
    QLinearGradient sky(0, 0, 0, horizon);
    sky.setColorAt(0, QColor(6, 2, 18));
    sky.setColorAt(1, QColor(42, 0, 70));
    p.fillRect(QRectF(0, 0, W, horizon), sky);
    p.fillRect(QRectF(0, horizon, W, H - horizon), QColor(6, 2, 18));
    p.setRenderHint(QPainter::Antialiasing);
    const double ph = std::fmod(t * 2.0, 1.0);
    auto line = [&](const QLineF& l, const QColor& c, double a) {
        QColor g = c;
        g.setAlphaF(0.22 * a);
        p.setPen(QPen(g, 6));
        p.drawLine(l);
        g.setAlphaF(a);
        p.setPen(QPen(g, 1.6));
        p.drawLine(l);
    };
    for (int k = 0; k < 34; ++k) { // across, coming toward you
        double z = k + 1 - ph;
        if (z <= 0.2) continue;
        double y = horizon + (H - horizon) * (1.2 / z);
        if (y > H) continue;
        line(QLineF(0, y, W, y), QColor(255, 40, 200), std::min(1.0, 1.6 / z));
    }
    for (int k = -14; k <= 14; ++k) // toward the horizon
        line(QLineF(W / 2 + k * 12, horizon, W / 2 + k * 120, H), QColor(0, 200, 255), 1.0);
    // the splash screen rises as a glowing panel, then grows to fill the window
    double show = easeOut(phase(t, 0.25, 0.95));
    if (show <= 0) return;
    double grow = smooth(phase(t, 1.55, 2.15));
    QRectF small((W - W * 0.56) / 2, H * 0.06 + 36 * (1 - show), W * 0.56, H * 0.56);
    QRectF r(small.x() * (1 - grow), small.y() * (1 - grow), small.width() + (W - small.width()) * grow,
             small.height() + (H - small.height()) * grow);
    p.setOpacity(show);
    QColor glow(0, 220, 255);
    for (int i = 3; i >= 1; --i) {
        glow.setAlphaF(0.18 * (1 - grow));
        p.setPen(QPen(glow, i * 4));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.adjusted(-2, -2, 2, 2));
    }
    // its reflection on the floor
    if (grow < 1) {
        QImage ref = base.mirrored(false, true);
        QRectF rr(r.x(), r.bottom() + 12, r.width(), r.height() * 0.45);
        p.save();
        p.setClipRect(rr);
        p.setOpacity(0.32 * show * (1 - grow));
        p.drawImage(QRectF(r.x(), rr.y(), r.width(), r.height()), ref);
        p.restore();
        QLinearGradient fade(0, rr.top(), 0, rr.bottom());
        fade.setColorAt(0, QColor(6, 2, 18, 0));
        fade.setColorAt(1, QColor(6, 2, 18, 255));
        p.fillRect(rr, fade);
    }
    p.setOpacity(show);
    p.drawImage(r, base);
    p.setOpacity(1);
}

// ------------------------------------------------------------------ marquee

const QImage& glowOf(const QImage& base, SplashEffectCache& c) {
    if (c.glowKey == base.cacheKey() && !c.glow.isNull()) return c.glow;
    QImage small = base.scaled(std::max(1, base.width() / 14), std::max(1, base.height() / 14), Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation);
    c.glow = small.scaled(base.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    c.glow.setDevicePixelRatio(base.devicePixelRatio());
    c.glowKey = base.cacheKey();
    return c.glow;
}

void marquee(QPainter& p, double t, const QImage& base, SplashEffectCache& c) {
    // a fluorescent tube warming up: dark, a few stutters, then lit with a glow that eases off
    static const double seq[] = {0.08, 0.08, 0.08, 0.08, 0.08, 0.08, 0.9, 0.1, 0.1, 1.0, 0.2, 0.15,
                                 0.15, 0.7, 0.3, 1.0, 1.0, 0.6, 1.0};
    const int n = int(sizeof(seq) / sizeof(seq[0]));
    const double stepLen = 0.05;
    double v = t < n * stepLen ? seq[int(t / stepLen)] : 1.0;
    double glow = t < n * stepLen ? 0.55 : 0.55 * (1 - smooth(phase(t, n * stepLen, 1.7)));
    p.drawImage(kFull, base);
    if (glow > 0.01) {
        p.save();
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        p.setOpacity(glow * std::max(v, 0.1));
        p.drawImage(kFull, glowOf(base, c));
        p.restore();
    }
    if (v < 0.95) p.fillRect(kFull, QColor(0, 0, 0, int(255 * (1 - v))));
}

// ------------------------------------------------------------------ card flip

void cardFlip(QPainter& p, double t, const QImage& base) {
    p.fillRect(kFull, QColor(8, 8, 12));
    double turn = phase(t, 0.05, 0.8);
    double e = 1 + 2.4 * std::pow(turn - 1, 3) + 1.4 * std::pow(turn - 1, 2); // overshoots, then settles
    double a = (88 * (1 - e)) * M_PI / 180;
    if (std::abs(std::cos(a)) < 0.02) return;
    const double persp = 1800;
    QPolygonF quad;
    for (QPointF c : {QPointF(-W / 2, -H / 2), QPointF(W / 2, -H / 2), QPointF(W / 2, H / 2), QPointF(-W / 2, H / 2)}) {
        double x3 = c.x() * std::cos(a), z3 = c.x() * std::sin(a);
        double k = persp / (persp + z3);
        quad << QPointF(W / 2 + x3 * k, H / 2 + c.y() * k);
    }
    QTransform tr;
    QPolygonF corners; // four corners (QPolygonF(QRectF) closes the polygon with a fifth)
    corners << kFull.topLeft() << kFull.topRight() << kFull.bottomRight() << kFull.bottomLeft();
    if (!QTransform::quadToQuad(corners, quad, tr)) return;
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setTransform(tr, true);
    p.drawImage(kFull, base);
    double shade = 0.45 + 0.55 * std::abs(std::cos(a));
    if (shade < 0.99) p.fillRect(kFull, QColor(0, 0, 0, int(255 * (1 - shade))));
    // a glint passing over it once it's settled
    double g = phase(t, 0.9, 1.45);
    if (g > 0 && g < 1) {
        double x = -W * 0.4 + g * W * 1.8;
        QLinearGradient lg(x - 90, 0, x + 30, 0);
        lg.setColorAt(0, QColor(255, 255, 255, 0));
        lg.setColorAt(0.5, QColor(255, 255, 255, 110));
        lg.setColorAt(1, QColor(255, 255, 255, 0));
        QPolygonF band;
        band << QPointF(x, 0) << QPointF(x + 120, 0) << QPointF(x - 80, H) << QPointF(x - 200, H);
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        p.setPen(Qt::NoPen);
        p.setBrush(lg);
        p.setClipRect(kFull);
        p.drawPolygon(band);
    }
    p.restore();
}

// ------------------------------------------------------------------ starfield

void starfield(QPainter& p, double t, const QImage& base, SplashEffectCache& c) {
    static const QColor colors[] = {QColor(255, 255, 255), QColor(180, 200, 255), QColor(255, 230, 180)};
    QRandomGenerator rng(1979);
    if (c.stars.isEmpty() || t < c.starsTime) {
        c.stars.clear();
        for (int i = 0; i < 300; ++i) {
            double r = 0.02 + rng.generateDouble() * 0.98;
            c.stars.append({rng.generateDouble() * 2 * M_PI, r, r, int(rng.bounded(3))});
        }
        c.starsTime = 0;
    }
    // fast at first, slowing down as the picture arrives
    double dt = std::clamp(t - c.starsTime, 0.0, 0.1);
    double speed = 0.09 * std::sin(M_PI * std::min(1.0, t / 1.6)) + 0.004;
    double grow = std::pow(1 + 6 * speed, dt * 25);
    for (auto& s : c.stars) {
        s.prevR = s.r;
        s.r = s.r * grow + 0.2 * speed * dt * 25;
        if (s.r > 1.6) {
            s.r = 0.02 + QRandomGenerator::global()->generateDouble() * 0.08;
            s.prevR = s.r;
        }
    }
    c.starsTime = t;
    p.fillRect(kFull, Qt::black);
    p.setRenderHint(QPainter::Antialiasing);
    const double R = W * 0.7;
    for (const auto& s : c.stars) {
        double from = std::min(s.prevR, s.r / (1 + 6 * speed));
        QPointF a(W / 2 + std::cos(s.angle) * from * R, H / 2 + std::sin(s.angle) * from * R);
        QPointF b(W / 2 + std::cos(s.angle) * s.r * R, H / 2 + std::sin(s.angle) * s.r * R);
        QColor col = colors[s.color];
        col.setAlphaF(std::min(1.0, s.r * 1.4));
        p.setPen(QPen(col, s.r < 0.6 ? 1.2 : 2.4));
        p.drawLine(a, b);
    }
    double lt = phase(t, 0.6, 1.55);
    if (lt > 0) {
        double s = 0.04 + 0.96 * easeOut(lt);
        QRectF r((W - W * s) / 2, (H - H * s) / 2, W * s, H * s);
        p.setOpacity(std::min(1.0, lt * 1.6));
        p.drawImage(r, base);
        p.setOpacity(1);
    }
}

// ------------------------------------------------------------------ high scores

struct ScoreRow { const char* rank; const char* score; const char* name; QColor color; };
const ScoreRow kScores[] = {{"1ST", "19742021", "CAC", QColor(255, 80, 80)},   // Ciro Alfredo Consentino (1974-2021)
                            {"2ND", "  850000", "ELR", QColor(255, 160, 40)},
                            {"3RD", "  720000", "MAM", QColor(255, 240, 60)},
                            {"4TH", "  610000", "ARC", QColor(80, 255, 90)},
                            {"5TH", "  500000", "HBM", QColor(60, 200, 255)},
                            {"6TH", "  400000", "DAT", QColor(200, 120, 255)}};

void arcadeText(QPainter& p, const QString& text, int px, double y, const QColor& color) {
    QFont f = arcadeFont(px);
    p.setFont(f);
    QFontMetricsF fm(f);
    double x = std::round((W - fm.horizontalAdvance(text)) / 2);
    p.setPen(QColor(0, 0, 0, 220));
    p.drawText(QPointF(x + px / 8.0 * 2, y + px / 8.0 * 2), text);
    p.setPen(color);
    p.drawText(QPointF(x, y), text);
}

void highScores(QPainter& p, double t, const QImage& base) {
    p.fillRect(kFull, Qt::black);
    const double rowStart = 0.15, rowGap = 0.17, blinkEnd = 2.2;
    arcadeText(p, "HIGH SCORES", 48, 100, Qt::white);
    arcadeText(p, "RANK    SCORE  NAME", 24, 170, QColor(120, 220, 255));
    int rows = std::clamp(int((t - rowStart) / rowGap) + 1, 0, 6);
    bool blinkOn = t < rowStart + 6 * rowGap || std::fmod(t, 0.32) < 0.2;
    for (int k = 0; k < rows; ++k) {
        const ScoreRow& r = kScores[k];
        QString line = QString("%1  %2  %3").arg(r.rank, r.score, k == 0 && !blinkOn ? QString("   ") : QString(r.name));
        arcadeText(p, line, 24, 240 + k * 52, r.color);
    }
    double fade = phase(t, blinkEnd, 2.9);
    if (fade > 0) {
        p.setOpacity(smooth(fade));
        p.drawImage(kFull, base);
        p.setOpacity(1);
    }
}

// ------------------------------------------------------------------ signal glitch

void glitch(QPainter& p, double t, const QImage& base) {
    static const double seq[] = {1.0, 0.9, 1.0, 0.7, 0.85, 0.6, 0.75, 0.5, 0.55, 0.4, 0.45,
                                 0.3, 0.32, 0.2, 0.25, 0.12, 0.1, 0.05, 0.6, 0.02, 0};
    const int n = int(sizeof(seq) / sizeof(seq[0]));
    int step = int(t / 0.045);
    if (step >= n) {
        p.drawImage(kFull, base);
        return;
    }
    double amount = seq[step];
    if (amount <= 0) {
        p.drawImage(kFull, base);
        return;
    }
    QRandomGenerator rng(quint32(step * 7919 + 3));
    const QImage src = base.convertToFormat(QImage::Format_RGB32);
    const int w = src.width(), h = src.height();
    const double px = w / W; // device pixels per artwork pixel
    QImage out(w, h, QImage::Format_RGB32);
    const int sh = int(28 * amount * px);
    const double dim = 0.6 + 0.4 * (1 - amount);
    for (int y = 0; y < h; ++y) { // red and blue apart
        const QRgb* in = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        QRgb* o = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int r = qRed(in[std::clamp(x - sh, 0, w - 1)]), g = qGreen(in[x]), b = qBlue(in[std::clamp(x + sh, 0, w - 1)]);
            o[x] = qRgb(int(r * dim), int(g * dim), int(b * dim));
        }
    }
    QVector<QRgb> tmp(w);
    for (int i = 0; i < int(9 * amount); ++i) { // torn bands
        int y0 = int(rng.bounded(std::max(1, h - int(20 * px)))), bh = int((3 + rng.bounded(20)) * px);
        int dx = int((int(rng.bounded(121)) - 60) * amount * px);
        for (int y = y0; y < std::min(h, y0 + bh); ++y) {
            QRgb* o = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < w; ++x) tmp[x] = o[((x - dx) % w + w) % w];
            std::copy(tmp.begin(), tmp.end(), o);
        }
    }
    for (int i = 0; i < int(14 * amount); ++i) { // noise lines
        int y = int(rng.bounded(h)), add = 40 + int(rng.bounded(100));
        QRgb* o = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x)
            o[x] = qRgb(std::min(255, qRed(o[x]) + add), std::min(255, qGreen(o[x]) + add), std::min(255, qBlue(o[x]) + add));
    }
    int bar = int(std::fmod((1 - amount) * H * 3, H) * px); // a rolling bar
    for (int y = bar; y < std::min(h, bar + int(10 * px)); ++y) {
        QRgb* o = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x)
            o[x] = qRgb(std::min(255, int(qRed(o[x]) * 1.25 + 20)), std::min(255, int(qGreen(o[x]) * 1.25 + 20)),
                        std::min(255, int(qBlue(o[x]) * 1.25 + 20)));
    }
    out.setDevicePixelRatio(base.devicePixelRatio());
    p.drawImage(kFull, out);
}

} // namespace

QFont arcadeFont(int pixelSize) {
    static QString family = [] {
        int id = QFontDatabase::addApplicationFont(":/fonts/PressStart2P-Regular.ttf");
        QStringList f = id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
        return f.isEmpty() ? QString("Courier New") : f.first();
    }();
    QFont f(family);
    f.setPixelSize(pixelSize);
    f.setStyleStrategy(QFont::NoAntialias);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

double splashEffectDuration(const QString& key) {
    if (key == "grid") return 2.2;
    if (key == "marquee") return 1.7;
    if (key == "card") return 1.5;
    if (key == "stars") return 1.6;
    if (key == "scores") return 2.95;
    if (key == "glitch") return 21 * 0.045;
    return 0;
}

void paintSplashEffect(QPainter& p, const QString& key, double t, const QImage& base, SplashEffectCache& c) {
    p.save();
    if (key == "grid") grid(p, t, base);
    else if (key == "marquee") marquee(p, t, base, c);
    else if (key == "card") cardFlip(p, t, base);
    else if (key == "stars") starfield(p, t, base, c);
    else if (key == "scores") highScores(p, t, base);
    else if (key == "glitch") glitch(p, t, base);
    else p.drawImage(kFull, base);
    p.restore();
}

void paintAfterEffect(QPainter& p, const QString& key, const QImage& base, SplashEffectCache& c) {
    Q_UNUSED(key);
    Q_UNUSED(c);
    p.drawImage(kFull, base);
}

void paintInsertCoin(QPainter& p, double t) {
    if (std::fmod(t, 0.64) >= 0.4) return;
    p.save();
    arcadeText(p, "INSERT COIN", 32, 536, QColor(255, 220, 40));
    p.restore();
}

} // namespace eld
