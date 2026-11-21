// SplashEffects - the splash screen's startup effects (new in EmuLoader ReLoaded): vector grid,
// marquee light-up, card flip, starfield warp, high scores and signal glitch, and the blinking
// INSERT COIN that can follow any of them.
//
// Each effect draws one frame for a time since it started, from the finished splash screen
// (the artwork with its texts); at its end it shows that picture as it is. Frames are drawn in the artwork's pixels (980 x 600).
#pragma once

#include <QFont>
#include <QImage>
#include <QString>
#include <QVector>

class QPainter;

namespace eld {

struct SplashEffectCache {
    // Marquee: the picture's glow
    qint64 glowKey = 0;
    QImage glow;
    // Starfield
    struct Star { double angle, r, prevR; int color; };
    QVector<Star> stars;
    double starsTime = -1;
    void reset() { stars.clear(); starsTime = -1; }
};

double splashEffectDuration(const QString& key);
// One frame of an effect at time t (seconds). base: the splash screen as it is now.
void paintSplashEffect(QPainter& p, const QString& key, double t, const QImage& base, SplashEffectCache& c);
// The finished picture after an effect.
void paintAfterEffect(QPainter& p, const QString& key, const QImage& base, SplashEffectCache& c);
// INSERT COIN, blinking (t: seconds since the effect ended).
void paintInsertCoin(QPainter& p, double t);
// Press Start 2P (bundled, SIL Open Font License), unsmoothed, at a multiple of 8 pixels.
QFont arcadeFont(int pixelSize);

} // namespace eld
