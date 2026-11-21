#include "ImageResample.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace eld {

namespace {

// Kernels as Graphics32 has them: TLinearKernel (width 1), TCosineKernel (width 1) and
// THermiteKernel with its default bias and tension of 0 (a Catmull-Rom cubic, width 2).
double kernel(Resampler r, double x) {
    x = std::abs(x);
    switch (r) {
    case Resampler::Linear: return x < 1 ? 1 - x : 0;
    case Resampler::Cosine: return x < 1 ? (std::cos(x * M_PI) + 1) * 0.5 : 0;
    case Resampler::Hermite:
        if (x < 1) return 1.5 * x * x * x - 2.5 * x * x + 1;
        if (x < 2) return -0.5 * x * x * x + 2.5 * x * x - 4 * x + 2;
        return 0;
    default: return x < 0.5 ? 1 : 0;
    }
}

double width(Resampler r) { return r == Resampler::Hermite ? 2 : 1; }

struct Contrib {
    int first = 0;
    std::vector<float> w;
};

// For each destination pixel along one axis: the source pixels and their weights.
std::vector<Contrib> contributions(int srcLen, int dstLen, Resampler r) {
    std::vector<Contrib> out(dstLen);
    const double scale = double(srcLen) / dstLen;
    const double stretch = std::max(1.0, scale); // made smaller: the kernel covers more pixels
    const double support = width(r) * stretch;
    for (int d = 0; d < dstLen; ++d) {
        const double center = (d + 0.5) * scale - 0.5;
        int lo = int(std::floor(center - support)), hi = int(std::ceil(center + support));
        lo = std::max(lo, 0);
        hi = std::min(hi, srcLen - 1);
        Contrib& c = out[d];
        c.first = lo;
        double sum = 0;
        for (int s = lo; s <= hi; ++s) {
            double v = kernel(r, (s - center) / stretch);
            c.w.push_back(float(v));
            sum += v;
        }
        if (std::abs(sum) < 1e-9) { // nothing under the kernel: the nearest pixel
            c.first = std::clamp(int(std::lround(center)), 0, srcLen - 1);
            c.w.assign(1, 1.f);
        } else {
            for (float& v : c.w) v = float(v / sum);
        }
    }
    return out;
}

} // namespace

QImage resampleImage(const QImage& src, const QSize& size, Resampler r) {
    if (src.isNull() || size.isEmpty()) return {};
    if (r == Resampler::Off) return src.scaled(size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    if (r == Resampler::Linear) return src.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QImage in = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int sw = in.width(), sh = in.height(), dw = size.width(), dh = size.height();
    // Across, into floats (4 channels), then down.
    const auto cx = contributions(sw, dw, r), cy = contributions(sh, dh, r);
    std::vector<float> mid(size_t(dw) * sh * 4);
    for (int y = 0; y < sh; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(in.constScanLine(y));
        float* o = &mid[size_t(y) * dw * 4];
        for (int x = 0; x < dw; ++x) {
            float a = 0, rr = 0, g = 0, b = 0;
            const Contrib& c = cx[x];
            for (size_t k = 0; k < c.w.size(); ++k) {
                QRgb p = line[c.first + int(k)];
                float w = c.w[k];
                a += qAlpha(p) * w; rr += qRed(p) * w; g += qGreen(p) * w; b += qBlue(p) * w;
            }
            o[x * 4] = a; o[x * 4 + 1] = rr; o[x * 4 + 2] = g; o[x * 4 + 3] = b;
        }
    }
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    auto clamp = [](float v, int hi) { return std::clamp(int(std::lround(v)), 0, hi); };
    for (int y = 0; y < dh; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        const Contrib& c = cy[y];
        for (int x = 0; x < dw; ++x) {
            float a = 0, rr = 0, g = 0, b = 0;
            for (size_t k = 0; k < c.w.size(); ++k) {
                const float* p = &mid[(size_t(c.first + int(k)) * dw + x) * 4];
                float w = c.w[k];
                a += p[0] * w; rr += p[1] * w; g += p[2] * w; b += p[3] * w;
            }
            int A = clamp(a, 255); // premultiplied: no color above the alpha
            line[x] = qRgba(clamp(rr, A), clamp(g, A), clamp(b, A), A);
        }
    }
    return out;
}

} // namespace eld
