#include "ElWidgets.h"

#include <QCoreApplication>
#include <QColorDialog>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "VclRuntime.h"

#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionButton>
#include <QStylePainter>
#include <algorithm>

namespace eld {

static int clampByte(int v) { return std::clamp(v, 0, 255); }

// ------------------------------------------------------------------ gradients
// Straight ports of TPanelEx's painting routines (lib/delphi7-components/PanelEx.pas).

// vgVista: a two-part vertical gradient (Color1 -> Color2, then a brighter
// tone back to Color2) averaged with a horizontal Color3 -> Color4 gradient.
static QImage vistaGradient(QSize sz, QColor c1, QColor c2, QColor c3, QColor c4) {
    int w = sz.width(), h = sz.height();
    QImage img(w, h, QImage::Format_RGB32);
    if (w < 1 || h < 1) return img;
    int rc1 = c1.red(), gc1 = c1.green(), bc1 = c1.blue();
    int rc2 = c2.red(), gc2 = c2.green(), bc2 = c2.blue();
    int rc3 = clampByte(rc1 + ((rc2 - rc1) * 7) / 2);
    int gc3 = clampByte(gc1 + ((gc2 - gc1) * 7) / 2);
    int bc3 = clampByte(bc1 + ((bc2 - bc1) * 7) / 2);
    std::vector<QRgb> col(h);
    int y = h / 2;
    for (int i = 0; i < y; ++i)
        col[i] = qRgb((rc1 + ((rc2 - rc1) * i) / y) & 0xFF, (gc1 + ((gc2 - gc1) * i) / y) & 0xFF,
                      (bc1 + ((bc2 - bc1) * i) / y) & 0xFF);
    if (rc2 > rc1) { rc3 = rc2; gc3 = gc2; bc3 = bc2; }
    for (int i = y; i < h; ++i)
        col[i] = qRgb((rc3 + ((rc2 - rc3) * i) / h) & 0xFF, (gc3 + ((gc2 - gc3) * i) / h) & 0xFF,
                      (bc3 + ((bc2 - bc3) * i) / h) & 0xFF);
    int r1 = c3.red(), g1 = c3.green(), b1 = c3.blue();
    int r2 = c4.red(), g2 = c4.green(), b2 = c4.blue();
    for (int x = 0; x < w; ++x) {
        int cr = (r1 + ((r2 - r1) * x) / w) & 0xFF, cg = (g1 + ((g2 - g1) * x) / w) & 0xFF,
            cb = (b1 + ((b2 - b1) * x) / w) & 0xFF;
        for (int yy = 0; yy < h; ++yy) {
            QRgb q = col[yy];
            int r = qRed(q), g = qGreen(q), b = qBlue(q);
            reinterpret_cast<QRgb*>(img.scanLine(yy))[x] =
                qRgb(((cr - r) >> 1) + r, ((cg - g) >> 1) + g, ((cb - b) >> 1) + b);
        }
    }
    return img;
}

// vgSimple: vertical gradient in `steps` bands.
static void simpleGradient(QPainter& p, QRect r, QColor top, QColor bottom, int steps) {
    steps = std::max(1, std::min(steps, std::max(1, r.height())));
    for (int i = 0; i < steps; ++i) {
        int y0 = r.top() + r.height() * i / steps, y1 = r.top() + r.height() * (i + 1) / steps;
        double t = steps == 1 ? 0 : double(i) / (steps - 1);
        QColor c(int(top.red() + (bottom.red() - top.red()) * t), int(top.green() + (bottom.green() - top.green()) * t),
                 int(top.blue() + (bottom.blue() - top.blue()) * t));
        p.fillRect(QRect(r.left(), y0, r.width(), y1 - y0), c);
    }
}

// vgMulti: vertical gradient through all four colors.
static void multiGradient(QPainter& p, QRect r, QColor c1, QColor c2, QColor c3, QColor c4) {
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0, c1); g.setColorAt(1.0 / 3, c2); g.setColorAt(2.0 / 3, c3); g.setColorAt(1, c4);
    p.fillRect(r, g);
}

// ---------------------------------------------------------------------- panel

ElPanel::ElPanel(QWidget* parent) : QFrame(parent) { setAttribute(Qt::WA_StyledBackground, false); }

void ElPanel::resizeEvent(QResizeEvent* e) {
    QFrame::resizeEvent(e);
    update();
}

void ElPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    QRect r = rect();
    QString cls = vcl::str(this, "vclClass");
    bool isEx = cls == "TPanelEx" || cls == "TPanelLabelEx";
    if (isEx) {
        // TPanelEx defaults (constructor): vgVista, white/silver/yellow/teal, 64 steps.
        QString style = vcl::str(this, "vclStyle");
        if (style.isEmpty()) style = "vgVista";
        QColor c1 = vcl::color(this, "vclColor1", Qt::white), c2 = vcl::color(this, "vclColor2", QColor(0xc0c0c0)),
               c3 = vcl::color(this, "vclColor3", Qt::yellow), c4 = vcl::color(this, "vclColor4", QColor(0x008080));
        int steps = property("vclSteps").isValid() ? property("vclSteps").toString().toInt() : 64;
        const vcl::NightLook& n = vcl::night();
        if (n.on && !property("elNoNight").toBool() && style != "vgSolid") {
            // Night Mode: the night panels' gradient ([Panels] BackgroundTopColor / BottomColor)
            simpleGradient(p, r, n.panelTop, n.panelBottom, 64);
        } else if (style == "vgVista" || style == "vgGlass") {
            QString key = style + c1.name() + c2.name() + c3.name() + c4.name();
            if (cache_.isNull() || cacheSize_ != size() || cacheKey_ != key) {
                cache_ = style == "vgVista" ? vistaGradient(size(), c1, c2, c3, c4) : QImage();
                if (style == "vgGlass") {
                    cache_ = QImage(size(), QImage::Format_RGB32);
                    QPainter gp(&cache_);
                    QRect top(0, 0, width(), height() / 2), bot(0, height() / 2, width(), height() - height() / 2);
                    QLinearGradient g1(top.topLeft(), top.bottomLeft()), g2(bot.topLeft(), bot.bottomLeft());
                    g1.setColorAt(0, c1); g1.setColorAt(1, c2.lighter(115));
                    g2.setColorAt(0, c2); g2.setColorAt(1, c2.darker(110));
                    gp.fillRect(top, g1); gp.fillRect(bot, g2);
                }
                cacheSize_ = size();
                cacheKey_ = key;
            }
            p.drawImage(0, 0, cache_);
        } else if (style == "vgSimple") {
            simpleGradient(p, r, c1, c2, steps);
        } else if (style == "vgDual") {
            int th = height() / 2;
            simpleGradient(p, QRect(0, 0, width(), th), c1, c2, steps);
            simpleGradient(p, QRect(0, th, width(), height() - th), c3, c4, steps);
        } else if (style == "vgMulti") {
            multiGradient(p, r, c1, c2, c3, c4);
        } else if (style == "vgSolid") {
            p.fillRect(r, c1);
        } else {
            p.fillRect(r, palette().button());
        }
        QString frames = vcl::str(this, "vclFrames");
        if (!frames.isEmpty()) {
            p.setPen(vcl::color(this, "vclColorFrame", QColor(0x008000)));
            if (frames.contains("frLeft")) p.drawLine(0, 0, 0, height() - 1);
            if (frames.contains("frTop")) p.drawLine(0, 0, width() - 1, 0);
            if (frames.contains("frRight")) p.drawLine(width() - 1, 0, width() - 1, height() - 1);
            if (frames.contains("frBottom")) p.drawLine(0, height() - 1, width() - 1, height() - 1);
        }
    } else {
        QColor bg = vcl::color(this, "vclColor");
        if (bg.isValid()) p.fillRect(r, bg);
        // TPanel default BevelOuter = bvRaised.
        QString outer = vcl::str(this, "vclBevelOuter");
        if (cls == "TPanel" && outer != "bvNone") {
            bool raised = outer != "bvLowered";
            QColor light = palette().light().color(), dark = palette().mid().color();
            p.setPen(raised ? light : dark);
            p.drawLine(0, 0, width() - 1, 0);
            p.drawLine(0, 0, 0, height() - 1);
            p.setPen(raised ? dark : light);
            p.drawLine(width() - 1, 0, width() - 1, height() - 1);
            p.drawLine(0, height() - 1, width() - 1, height() - 1);
        }
    }
    QString cap = vcl::str(this, "vclCaption");
    if (!cap.isEmpty()) {
        p.setPen(palette().windowText().color());
        p.drawText(r, Qt::AlignCenter, QString(cap).remove('&'));
    }
}

// ---------------------------------------------------------------------- label

ElLabel::ElLabel(QWidget* parent) : QLabel(parent) {}

void ElLabel::paintEvent(QPaintEvent* e) {
    QString cls = vcl::str(this, "vclClass");
    bool shadow = cls == "TShadowLabel" && vcl::str(this, "vclShadowEnabled") != "False";
    if (!shadow || text().isEmpty()) { QLabel::paintEvent(e); return; }
    QPainter p(this);
    QRect r = contentsRect();
    int flags = int(alignment()) | (wordWrap() ? Qt::TextWordWrap : 0);
    QColor sc = isEnabled() ? vcl::color(this, "vclShadowColor", QColor(0x808080)) : QColor(0x505050);
    p.setFont(font());
    p.setPen(sc);
    p.drawText(r.translated(1, 1), flags, text());
    p.setPen(isEnabled() ? palette().windowText().color() : QColor(0x808080));
    p.drawText(r, flags, text());
}

// --------------------------------------------------------------------- button

ElButton::ElButton(QWidget* parent) : QPushButton(parent) { setAutoDefault(false); }

void ElButton::setCaption(const QString& caption) {
    caption_ = caption;
    update();
}

void ElButton::paintEvent(QPaintEvent* e) {
    if (property("vclSeparator").toBool()) return; // TToolButton separator
    if (caption_.isNull()) {
        QPushButton::paintEvent(e);
        return;
    }
    QStylePainter p(this);
    QStyleOptionButton opt;
    initStyleOption(&opt);
    opt.text.clear();
    opt.icon = QIcon();
    p.drawControl(QStyle::CE_PushButton, opt); // the background (hover, pressed) only
    QIcon::Mode mode = !isEnabled() ? QIcon::Disabled : QIcon::Normal;
    QSize is = iconSize();
    int lineH = fontMetrics().height();
    int lines = caption_.isEmpty() ? 0 : std::max(1, property("elCaptionLines").toInt());
    int top = caption_.isEmpty() ? (height() - is.height()) / 2 : 3;
    QRect iconRect(QPoint((width() - is.width()) / 2, top), is);
    if (isDown() || isChecked()) iconRect.translate(1, 1);
    icon().paint(&p, iconRect, Qt::AlignCenter, mode, isChecked() ? QIcon::On : QIcon::Off);
    if (caption_.isEmpty()) return;
    QRect textRect(2, iconRect.bottom() + 2, width() - 4, lineH * lines + 1);
    p.setPen(palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText));
    p.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, caption_);
}

// ---------------------------------------------------------------------- image

ElImage::ElImage(QWidget* parent) : QLabel(parent) {
    setAlignment(Qt::AlignCenter);
    setScaledContents(false);
}

// ---------------------------------------------------------------------- shape

void ElShape::paintEvent(QPaintEvent*) {
    QPainter p(this);
    QString cls = vcl::str(this, "vclClass");
    if (cls == "TShape") {
        QColor brush = vcl::color(this, "vclBrushColor", Qt::white), pen = vcl::color(this, "vclPenColor", Qt::black);
        p.setPen(pen);
        p.setBrush(brush);
        QString shape = vcl::str(this, "vclShape");
        QRect r = rect().adjusted(0, 0, -1, -1);
        if (shape == "stEllipse" || shape == "stCircle") p.drawEllipse(r);
        else if (shape.startsWith("stRound")) p.drawRoundedRect(r, 6, 6);
        else p.drawRect(r);
    } else { // TBevel: etched line/box
        p.setPen(palette().mid().color());
        QString shape = vcl::str(this, "vclShape");
        if (shape == "bsTopLine") p.drawLine(0, 0, width(), 0);
        else if (shape == "bsBottomLine") p.drawLine(0, height() - 2, width(), height() - 2);
        else if (shape == "bsLeftLine") p.drawLine(0, 0, 0, height());
        else if (shape == "bsRightLine") p.drawLine(width() - 2, 0, width() - 2, height());
        else p.drawRect(rect().adjusted(0, 0, -2, -2));
        p.setPen(palette().light().color());
        if (shape == "bsTopLine") p.drawLine(0, 1, width(), 1);
        else if (shape == "bsBottomLine") p.drawLine(0, height() - 1, width(), height() - 1);
    }
}

// ------------------------------------------------------------------ color box

namespace {
struct NamedColor { const char* name; int tcolor; };
// TColorBox lists: standard, extended (Money Green ...), system colors (clScrollBar ...).
const NamedColor kColors[] = {
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Black"), 0x000000},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Maroon"), 0x000080},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Green"), 0x008000},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Olive"), 0x008080},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Navy"), 0x800000},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Purple"), 0x800080},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Teal"), 0x808000},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Gray"), 0x808080},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Silver"), 0xC0C0C0},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Red"), 0x0000FF},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Lime"), 0x00FF00},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Yellow"), 0x00FFFF},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Blue"), 0xFF0000},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Fuchsia"), 0xFF00FF},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Aqua"), 0xFFFF00},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "White"), 0xFFFFFF},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Money Green"), 0xC0DCC0},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Sky Blue"), 0xF0CAA6},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Cream"), 0xF0FBFF},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Medium Gray"), 0xA4A0A0},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Scroll Bar"), int(0xFF000000)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Background"), int(0xFF000001)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Active Caption"), int(0xFF000002)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Inactive Caption"), int(0xFF000003)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Menu"), int(0xFF000004)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Window"), int(0xFF000005)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Window Frame"), int(0xFF000006)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Menu Text"), int(0xFF000007)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Window Text"), int(0xFF000008)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Caption Text"), int(0xFF000009)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Active Border"), int(0xFF00000A)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Inactive Border"), int(0xFF00000B)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Application Workspace"), int(0xFF00000C)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Highlight"), int(0xFF00000D)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Highlight Text"), int(0xFF00000E)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Button Face"), int(0xFF00000F)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Button Shadow"), int(0xFF000010)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Gray Text"), int(0xFF000011)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Button Text"), int(0xFF000012)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Inactive Caption Text"), int(0xFF000013)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Button Highlight"), int(0xFF000014)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "3D Dark Shadow"), int(0xFF000015)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "3D Light"), int(0xFF000016)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Info Text"), int(0xFF000017)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Info Background"), int(0xFF000018)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Hot Light"), int(0xFF00001A)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Gradient Active Caption"), int(0xFF00001B)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Gradient Inactive Caption"), int(0xFF00001C)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Menu Highlight"), int(0xFF00001D)},
    {QT_TRANSLATE_NOOP("eld::ElColorBox", "Menu Bar"), int(0xFF00001E)},
};
// Windows 10 default system colors (GetSysColor), used off Windows.
const QRgb kSysColors[31] = {
    0xC8C8C8, 0x000000, 0x99B4D1, 0xBFCDDB, 0xF0F0F0, 0xFFFFFF, 0x646464, 0x000000, 0x000000, 0x000000,
    0xB4B4B4, 0xF4F7FC, 0xABABAB, 0x0078D7, 0xFFFFFF, 0xF0F0F0, 0xA0A0A0, 0x6D6D6D, 0x000000, 0x000000,
    0xFFFFFF, 0x696969, 0xE3E3E3, 0x000000, 0xFFFFE1, 0x000000, 0x0066CC, 0xB9D1EA, 0xD7E4F2, 0x3399FF, 0xF0F0F0};
QIcon swatch(const QColor& c) {
    QPixmap px(16, 12);
    px.fill(c);
    QPainter p(&px);
    p.setPen(Qt::black);
    p.drawRect(0, 0, 15, 11);
    return QIcon(px);
}
} // namespace

QColor ElColorBox::toQColor(int t) {
    if (t < 0) { // system color $FF0000nn
        int i = t & 0xFF;
#ifdef Q_OS_WIN
        DWORD c = GetSysColor(i);
        return QColor(GetRValue(c), GetGValue(c), GetBValue(c));
#else
        return QColor::fromRgb(i < 31 ? kSysColors[i] : 0);
#endif
    }
    return QColor(t & 0xFF, (t >> 8) & 0xFF, (t >> 16) & 0xFF);
}

int ElColorBox::fromQColor(const QColor& c) { return c.red() | (c.green() << 8) | (c.blue() << 16); }

ElColorBox::ElColorBox(QWidget* parent) : QComboBox(parent) {
    setIconSize(QSize(16, 12));
    // as wide as a dozen characters, not measured on its ~50 color names: a window with many of
    // these laid them out slowly (each layout pass measured every name of every box)
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMinimumContentsLength(12);
    fill();
    connect(this, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        if (i == 0) { // "Custom..." (cbCustomColor)
            QColor c = QColorDialog::getColor(toQColor(last_), this, "Color");
            setTColor(c.isValid() ? fromQColor(c) : last_);
        } else {
            last_ = itemData(i).toInt();
        }
        emit colorChanged();
    });
}

void ElColorBox::fill() {
    clear();
    addItem(QCoreApplication::translate("eld::ElColorBox", "Custom..."));
    for (const NamedColor& n : kColors)
        addItem(swatch(toQColor(n.tcolor)), QCoreApplication::translate("eld::ElColorBox", n.name), n.tcolor);
}

int ElColorBox::defaultTColor() const {
    QColor c = vcl::color(this, "vclDefaultColorColor", Qt::black); // TColorBox default: clBlack
    return fromQColor(c);
}

int ElColorBox::tcolor() const { return last_; }

void ElColorBox::setTColor(int c) {
    last_ = c;
    int i = -1;
    for (int k = 1; k < count() && i < 0; ++k)
        if (itemData(k).toInt() == c) i = k;
    if (i < 0) { // a color that isn't listed: shown as the custom entry, like TColorBox
        setItemIcon(0, swatch(toQColor(c)));
        setItemText(0, QCoreApplication::translate("eld::ElColorBox", "Custom..."));
        setItemData(0, c);
        i = 0;
    }
    setCurrentIndex(i);
}

// ------------------------------------------------------------------- splitter

ElSplitter::ElSplitter(QWidget* parent) : QFrame(parent) { setMouseTracking(true); }

void ElSplitter::setTmsStyle(int style, const QColor& single, const QColor& singleHot) {
    style_ = style;
    single_ = single;
    singleHot_ = singleHot;
    update();
}

void ElSplitter::paintEvent(QPaintEvent*) {
    // TCustomSplitterEx.Paint / SetStyle (SplitterEx.pas). Colors are TColor ($BBGGRR).
    static const char* names[] = {"tsSilver", "tsSolidColor", "tsOffice2003Blue", "tsOffice2003Silver",
                                  "tsOffice2003Olive", "tsOffice2003Classic", "tsOffice2007Luna", "tsOffice2007Silver",
                                  "tsOffice2007Obsidian", "tsWindowsXP", "tsWhidbey", "tsWindowsVista", "tsWindows7",
                                  "tsTerminal", "tsOffice2010Blue", "tsOffice2010Silver", "tsOffice2010Black"};
    struct Look { int from, to, hot, hotTo; };
    const int none = -1, btnFace = int(0xFF00000F), highlight = int(0xFF00000D);
    static const Look looks[] = {
        {0xFFFFFF, 0xC0C0C0, 0xFFFFFF, 0x808080}, {0, 0, 0, 0},
        {0xFDEADA, 0xE4AE88, 0x00D3F8FF, 0x0076C1FF}, {0xECE2E1, 0xB39698, 0x00D3F8FF, 0x0076C1FF},
        {0xCFF0EA, 0x8CC0B1, 0x00D3F8FF, 0x0076C1FF}, {btnFace, none, 0x00D2BDB6, none},
        {0xFAF1E9, 0xEDD8C7, 0x00D3F8FF, 0x0076C1FF}, {0xF9F5F3, 0xE7DCD5, 0x00D3F8FF, 0x0076C1FF},
        {0xCFC6C1, 0xC5BBB4, 0x00D3F8FF, 0x0076C1FF}, {btnFace, none, 0x00D2BDB6, none},
        {0xFFFFFF, 0x00E3F0F2, 0x00D3F8FF, 0x0076C1FF}, {0xFFFDF9, 0xFFFAF0, 0xFEF9F0, 0xFDF0D7},
        {0xFDFBFA, 0xFDF3EB, 0xFCEBDC, 0xFCDBC1}, {0xC0C0C0, 0xC0C0C0, highlight, highlight},
        {0xFDF6EF, 0xF0DAC7, 0x7BEEFF, 0x6CD0FF}, {0xFFFFFF, 0xEDE5E0, 0x7BEEFF, 0x6CD0FF},
        {0xBFBFBF, 0x919191, 0x7BEEFF, 0x6CD0FF}};
    int st = style_;
    if (st < 0) {
        st = 6;
        QString n = vcl::str(this, "vclStyle");
        for (int i = 0; i < 17; ++i)
            if (n == names[i]) st = i;
    }
    QPainter p(this);
    QRect r = rect();
    QString al = vcl::str(this, "vclAlign");
    bool vertical = al == "alLeft" || al == "alRight"; // dtAuto: gradient across the bar
    if (st == 1) {
        p.fillRect(r, hot_ ? singleHot_ : single_);
    } else {
        const Look& l = looks[std::clamp(st, 0, 16)];
        int a = hot_ ? l.hot : l.from, b = hot_ ? l.hotTo : l.to;
        QColor ca = ElColorBox::toQColor(a), cb = b == none ? ca : ElColorBox::toQColor(b);
        // DrawGradient: 64 bands
        const int steps = 64;
        int len = vertical ? r.width() : r.height();
        for (int i = 0; i < steps; ++i) {
            QColor c(ca.red() + (cb.red() - ca.red()) * i / steps, ca.green() + (cb.green() - ca.green()) * i / steps,
                     ca.blue() + (cb.blue() - ca.blue()) * i / steps);
            int s0 = i * len / steps, s1 = (i + 1) * len / steps + 1;
            p.fillRect(vertical ? QRect(s0, 0, s1 - s0, r.height()) : QRect(0, s0, r.width(), s1 - s0), c);
        }
    }
    if (grip_) { // sgDots: 9 dots, white with a black dot offset by one pixel
        if (vertical) {
            int x = r.width() / 2, y0 = (r.height() - 34) / 2;
            for (int i = 0; i < 9; ++i) {
                p.fillRect(x, y0 + 1 + i * 4, 2, 2, Qt::white);
                p.fillRect(x - 1, y0 + i * 4, 2, 2, Qt::black);
            }
        } else {
            int y = r.height() / 2, x0 = (r.width() - 34) / 2;
            for (int i = 0; i < 9; ++i) {
                p.fillRect(x0 + 1 + i * 4, y, 2, 2, Qt::white);
                p.fillRect(x0 + i * 4, y - 1, 2, 2, Qt::black);
            }
        }
    }
}

QWidget* ElSplitter::target() const {
    // VCL: the splitter resizes the nearest sibling aligned to the same side that
    // sits just before it (left of an alLeft splitter, right of an alRight one...).
    QString al = vcl::str(this, "vclAlign");
    QWidget* best = nullptr;
    int bestDist = INT_MAX;
    for (QObject* o : parentWidget()->children()) {
        auto* w = qobject_cast<QWidget*>(o);
        if (!w || w == this || w->isHidden() || vcl::str(w, "vclAlign") != al) continue;
        int d = INT_MAX;
        if (al == "alLeft") d = x() - (w->x() + w->width());
        else if (al == "alRight") d = w->x() - (x() + width());
        else if (al == "alTop") d = y() - (w->y() + w->height());
        else if (al == "alBottom") d = w->y() - (y() + height());
        if (d >= 0 && d < bestDist) { bestDist = d; best = w; }
    }
    return best;
}

void ElSplitter::mousePressEvent(QMouseEvent* e) {
    target_ = target();
    pressPos_ = e->globalPosition().toPoint();
    if (target_) {
        QString al = vcl::str(this, "vclAlign");
        startSize_ = (al == "alLeft" || al == "alRight") ? target_->width() : target_->height();
    }
}

void ElSplitter::mouseMoveEvent(QMouseEvent* e) {
    if (!target_ || !(e->buttons() & Qt::LeftButton)) {
        QString al = vcl::str(this, "vclAlign");
        setCursor((al == "alLeft" || al == "alRight") ? Qt::SplitHCursor : Qt::SplitVCursor);
        return;
    }
    QPoint d = e->globalPosition().toPoint() - pressPos_;
    QString al = vcl::str(this, "vclAlign");
    int s = startSize_;
    if (al == "alLeft") s += d.x();
    else if (al == "alRight") s -= d.x();
    else if (al == "alTop") s += d.y();
    else s -= d.y();
    s = std::max(30, s);
    if (al == "alLeft" || al == "alRight") target_->resize(s, target_->height());
    else target_->resize(target_->width(), s);
    target_->setProperty("vclDesignW", target_->width());
    target_->setProperty("vclDesignH", target_->height());
    vcl::relayout(parentWidget());
    emit moved();
}

void ElSplitter::mouseReleaseEvent(QMouseEvent*) { target_ = nullptr; }

} // namespace eld
