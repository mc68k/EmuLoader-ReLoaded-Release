// ImageView - one image of the image panel (TImage32 "Images" / "ImageScr[n]" in the original):
// scaled per the image menu (Stretch, Stretch Larger Images Only, Disable Aspect Ratio) over the
// category's background color, with the image hint box (category, size and type, zipped or not)
// shown while the mouse is over the image.
#pragma once

#include "ImageResample.h"

#include <QIcon>
#include <QImage>
#include <QWidget>

namespace eld {

class ImageView : public QWidget {
    Q_OBJECT
public:
    explicit ImageView(QWidget* parent = nullptr);

    struct Scale { bool stretch = true, largerOnly = false, ignoreAspect = false; };
    struct Hint {  // Preferences page 2 "Image Hint Box" (lightmode.ini [Images] HintBox_*)
        QColor color = QColor(0x33, 0x99, 0xff), frameColor = QColor(0x33, 0x99, 0xff), text = Qt::white,
               shadow = Qt::black;
        bool frame = true, textShadow = true, italic = false;
        int opacity = 200, position = 4;  // 1..8: top/left ... bottom/right
    };
    void setImage(const QImage& img, bool placeholder);
    void setBackground(const QColor& c) { background_ = c; update(); }
    void setScale(const Scale& s) { scale_ = s; update(); }
    void setHint(const Hint& h) { hint_ = h; update(); }
    void setHintText(const QIcon& icon, const QString& title, const QString& details, bool zipped);
    // No image: the category's picture, faded, with a heading and why ("No folder is set ...")
    // drawn in the panel's colors, in the user's language. clickable: a click asks to fix it
    // (notFoundClicked: Settings > Pictures). An empty heading: none.
    void setNotFound(const QIcon& picture, const QString& heading, const QString& reason, bool clickable);
    bool notFoundClickable() const { return notFoundClickable_; }
    QString notFoundText() const { return notFoundHeading_.isEmpty() ? QString() : notFoundHeading_ + '\n' + notFoundReason_; }
    const QImage& image() const { return image_; }
    // Menu Options > Images > Resampler, for every image of the panel
    static void setResampler(Resampler r) { resampler_ = r; }
    static Resampler resampler() { return resampler_; }

signals:
    void contextMenuWanted(const QPoint& globalPos);
    // +1 / -1 (wheel down / up): next / previous category, or with Ctrl the game's next / previous image
    void wheelStep(int step, bool ctrl);
    void notFoundClicked();
    void leftClicked(); // any left click on the panel

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override { hover_ = true; update(); }
    void leaveEvent(QEvent*) override { hover_ = false; update(); }
    void contextMenuEvent(QContextMenuEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRect targetRect() const;
    QImage image_;
    bool placeholder_ = false, hover_ = false;
    QColor background_ = Qt::black;
    Scale scale_;
    Hint hint_;
    QIcon hintIcon_;
    QString hintTitle_, hintDetails_;
    QIcon notFoundPicture_;
    QString notFoundHeading_, notFoundReason_;
    bool notFoundClickable_ = false;
    void paintNotFound(QPainter& p);
    bool hintZipped_ = false;
    int wheelDelta_ = 0;
    static inline Resampler resampler_ = Resampler::Cosine;
    QImage scaled_; // the image at its drawn size, through the resampler
    qint64 scaledKey_ = 0;
    QSize scaledSize_;
    Resampler scaledWith_ = Resampler::Off;
};

} // namespace eld
