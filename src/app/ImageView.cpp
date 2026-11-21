#include "ImageView.h"

#include <cstdlib>

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

namespace eld {

ImageView::ImageView(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void ImageView::setImage(const QImage& img, bool placeholder) {
    notFoundHeading_.clear(); // (an image, or the user's own not-found picture)
    notFoundReason_.clear();
    notFoundClickable_ = false;
    unsetCursor();
    image_ = img;
    placeholder_ = placeholder;
    update();
}

void ImageView::setHintText(const QIcon& icon, const QString& title, const QString& details, bool zipped) {
    hintIcon_ = icon;
    hintTitle_ = title;
    hintDetails_ = details;
    hintZipped_ = zipped;
    update();
}

void ImageView::setNotFound(const QIcon& picture, const QString& heading, const QString& reason, bool clickable) {
    image_ = QImage();
    placeholder_ = true;
    notFoundPicture_ = picture;
    notFoundHeading_ = heading;
    notFoundReason_ = reason;
    notFoundClickable_ = clickable;
    setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}

void ImageView::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && image_.isNull() && notFoundClickable_ && !notFoundHeading_.isEmpty()) emit notFoundClicked();
    if (e->button() == Qt::LeftButton) emit leftClicked();
    QWidget::mouseReleaseEvent(e);
}

// The category's picture in gray, faded into the background, the heading and the reason under it,
// in colors that read on the panel's background (light or dark)
void ImageView::paintNotFound(QPainter& p) {
    const bool dark = background_.lightness() < 128;
    const QColor head = dark ? QColor(225, 225, 230) : QColor(60, 60, 66);
    const QColor sub = dark ? QColor(165, 165, 175) : QColor(105, 105, 115);
    const int side = std::min(std::min(width(), height()) * 45 / 100, 220);
    QFont hf = font();
    hf.setBold(true);
    hf.setPixelSize(std::clamp(std::min(width() / 12, height() / 11), 15, 34));
    QFont rf = font();
    rf.setPixelSize(std::clamp(std::min(width() / 20, height() / 18), 13, 20));
    const int textWidth = std::max(80, width() - 24);
    const QRect reasonBox = QFontMetrics(rf).boundingRect(QRect(0, 0, textWidth, 1000), Qt::AlignHCenter | Qt::TextWordWrap, notFoundReason_);
    const int headHeight = QFontMetrics(hf).height();
    const bool wide = width() > height() * 2 && side >= 24; // a marquee: the picture beside the words
    QPixmap pic;
    if (side >= 24 && !notFoundPicture_.isNull()) {
        const QSize avail = notFoundPicture_.availableSizes().value(0);
        QSize s = avail.isEmpty() ? QSize(side, side) : avail.scaled(side, side, Qt::KeepAspectRatio);
        QImage img = notFoundPicture_.pixmap(s).toImage().convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < img.height(); ++y) {
            auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const int g = qGray(line[x]);
                line[x] = qRgba(g, g, g, qAlpha(line[x]) * (dark ? 45 : 38) / 100);
            }
        }
        pic = QPixmap::fromImage(img);
    }
    if (wide) {
        const int textW = std::min(width() - pic.width() - 64, 520);
        const QRect rb = QFontMetrics(rf).boundingRect(QRect(0, 0, std::max(80, textW), 1000), Qt::TextWordWrap, notFoundReason_);
        const int blockW = pic.width() + 24 + std::max(QFontMetrics(hf).horizontalAdvance(notFoundHeading_), rb.width());
        int x = std::max(16, (width() - blockW) / 2);
        if (!pic.isNull()) p.drawPixmap(x, (height() - pic.height()) / 2, pic);
        x += pic.width() + 24;
        const int top = (height() - headHeight - 6 - rb.height()) / 2;
        p.setFont(hf);
        p.setPen(head);
        p.drawText(QRect(x, top, width() - x - 8, headHeight), Qt::AlignLeft | Qt::AlignVCenter, notFoundHeading_);
        p.setFont(rf);
        p.setPen(sub);
        p.drawText(QRect(x, top + headHeight + 6, width() - x - 8, rb.height()), Qt::AlignLeft | Qt::TextWordWrap, notFoundReason_);
        return;
    }
    const int total = (pic.isNull() ? 0 : pic.height() + 16) + headHeight + 8 + reasonBox.height();
    int y = std::max(4, (height() - total) / 2);
    if (!pic.isNull()) {
        p.drawPixmap((width() - pic.width()) / 2, y, pic);
        y += pic.height() + 16;
    }
    p.setFont(hf);
    p.setPen(head);
    p.drawText(QRect(8, y, width() - 16, headHeight), Qt::AlignHCenter | Qt::AlignVCenter, notFoundHeading_);
    y += headHeight + 8;
    p.setFont(rf);
    p.setPen(sub);
    p.drawText(QRect((width() - textWidth) / 2, y, textWidth, reasonBox.height()), Qt::AlignHCenter | Qt::TextWordWrap, notFoundReason_);
}

QRect ImageView::targetRect() const {
    // SetImageScaleMode: smStretch (no aspect), smResize (fit, keep aspect), smNormal (as is);
    // the image is centered (baCenter).
    if (image_.isNull()) return {};
    QSize img = image_.size(), area = size();
    QSize s = img;
    bool fit = scale_.stretch && (!scale_.largerOnly || img.width() > area.width() || img.height() > area.height());
    if (scale_.stretch && scale_.ignoreAspect) s = area;
    else if (fit) s = img.scaled(area, Qt::KeepAspectRatio);
    if (placeholder_) s = img.scaled(area * 0.6, Qt::KeepAspectRatio).boundedTo(img); // the logo, not blown up
    return QRect(QPoint((area.width() - s.width()) / 2, (area.height() - s.height()) / 2), s);
}

void ImageView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), background_);
    if (!image_.isNull()) {
        const QRect t = targetRect();
        const qreal dpr = devicePixelRatioF();
        const QSize px = (QSizeF(t.size()) * dpr).toSize();
        if (px == image_.size() || px.isEmpty()) {
            p.drawImage(t, image_);
        } else {
            if (scaledKey_ != image_.cacheKey() || scaledSize_ != px || scaledWith_ != resampler_) {
                scaled_ = resampleImage(image_, px, resampler_);
                scaled_.setDevicePixelRatio(dpr);
                scaledKey_ = image_.cacheKey();
                scaledSize_ = px;
                scaledWith_ = resampler_;
            }
            p.drawImage(t.topLeft(), scaled_);
        }
    } else if (!notFoundHeading_.isEmpty()) {
        paintNotFound(p);
    }
    if (!hover_ || hintTitle_.isEmpty()) return;
    // Image hint box (UpdateImageHintPanelText / UpdateImageHintBoxPosition)
    QFont f = font();
    f.setPointSize(10);
    f.setItalic(hint_.italic);
    QFont fd = font();
    fd.setPointSize(9);
    int tw = std::max(QFontMetrics(f).horizontalAdvance(hintTitle_), QFontMetrics(fd).horizontalAdvance(hintDetails_));
    QSize box(tw + 16 + 8 + 20, 48);
    int x = 20, y = 20;
    switch (hint_.position) {
    case 2: x = (width() - box.width()) / 2; break;
    case 3: x = width() - box.width() - 20; break;
    case 4: y = (height() - box.height()) / 2; break;
    case 5: x = width() - box.width() - 20; y = (height() - box.height()) / 2; break;
    case 6: y = height() - box.height() - 20; break;
    case 7: x = (width() - box.width()) / 2; y = height() - box.height() - 20; break;
    case 8: x = width() - box.width() - 20; y = height() - box.height() - 20; break;
    default: break;
    }
    QRect r(QPoint(x, y), box);
    QColor bg = hint_.color;
    bg.setAlpha(std::clamp(hint_.opacity, 0, 255));
    p.fillRect(r, bg);
    if (hint_.frame) {
        p.setPen(hint_.frameColor);
        p.drawRect(r.adjusted(0, 0, -1, -1));
    }
    hintIcon_.paint(&p, QRect(r.left() + 8, r.top() + 8, 16, 16));
    auto text = [&](const QFont& font, int top, const QString& s) {
        p.setFont(font);
        QPoint at(r.left() + 8 + 16 + 4, r.top() + top + QFontMetrics(font).ascent());
        if (hint_.textShadow) {
            p.setPen(hint_.shadow);
            p.drawText(at + QPoint(1, 1), s);
        }
        p.setPen(hint_.text);
        p.drawText(at, s);
    };
    text(f, 5, hintTitle_);
    text(fd, 25, hintDetails_ + (hintZipped_ ? "  [zip]" : ""));
}

void ImageView::contextMenuEvent(QContextMenuEvent* e) { emit contextMenuWanted(e->globalPos()); }

void ImageView::wheelEvent(QWheelEvent* e) {
    // One step per wheel notch (120); touchpads send smaller deltas, so add them up.
    wheelDelta_ += e->angleDelta().y();
    bool ctrl = e->modifiers() & Qt::ControlModifier;
    while (std::abs(wheelDelta_) >= 120) {
        int step = wheelDelta_ < 0 ? 1 : -1;
        wheelDelta_ += step * 120;
        emit wheelStep(step, ctrl);
    }
    e->accept();
}

} // namespace eld
