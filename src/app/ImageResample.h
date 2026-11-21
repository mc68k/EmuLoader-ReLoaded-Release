// ImageResample - the image panel's Resampler (Menu Options > Images > Resampler; the original's
// Graphics32 resamplers, [Images] ImageResampler): Off (nearest pixel), Linear, Cosine (the
// default: sharp, keeps the picture's details) and Hermite (sharper still).
#pragma once

#include <QImage>
#include <QSize>

namespace eld {

enum class Resampler { Off = 0, Linear = 1, Cosine = 2, Hermite = 3 };

// The image at another size, through the resampler's kernel (separable, widened when the image
// is made smaller, so every source pixel counts).
QImage resampleImage(const QImage& src, const QSize& size, Resampler r);

} // namespace eld
