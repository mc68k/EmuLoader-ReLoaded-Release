#include "VclRuntime.h"

#include "ScriptFonts.h"
#include "ElWidgets.h"

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QGroupBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTextDocument>
#include <QButtonGroup>
#include <QComboBox>
#include <QEvent>
#include <QHash>
#include <QLabel>
#include <QBoxLayout>
#include <functional>
#include <typeinfo>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSlider>
#include <cmath>
#include <QTextEdit>
#include <QWidget>
#include <algorithm>

namespace eld::vcl {

QString str(const QWidget* w, const char* prop) {
    QVariant v = w->property(prop);
    return v.isValid() ? v.toString() : QString();
}

static NightLook g_night;
void setNight(const NightLook& n) { g_night = n; }
const NightLook& night() { return g_night; }

QColor nightColor(const QColor& c) {
    if (!c.isValid()) return c;
    float h, s, l, a;
    c.getHslF(&h, &s, &l, &a);
    l = 1.0f - l;
    // very dark (was very light): a touch of the night background's blue-gray, not pure black
    if (l < 0.09f) l = 0.09f;
    return QColor::fromHslF(h < 0 ? 0 : h, s * 0.85f, l, a);
}

QColor color(const QWidget* w, const char* prop, const QColor& fallback) {
    QString s = str(w, prop);
    QColor c = s.isEmpty() ? fallback : QColor(s);
    if (g_night.on && c.isValid() && !w->property("elNoNight").toBool()) {
        // only what's made for a light window turns around: light backgrounds, dark texts (a form
        // made dark already, as Customize Night Mode Colors, keeps its colors)
        const bool text = qstrcmp(prop, "vclFontColor") == 0;
        if (text ? c.lightnessF() < 0.5 : c.lightnessF() > 0.5) return nightColor(c);
    }
    return c;
}

static QString mapFamily(const QString& f) {
#ifdef Q_OS_WIN
    if (f == "MS Sans Serif") return "Microsoft Sans Serif";
    return f;
#else
    // Windows fonts are usually missing elsewhere: pick close metric equivalents.
    static const QHash<QString, QString> m = {
        {"Segoe UI", "Noto Sans"}, {"Tahoma", "DejaVu Sans"}, {"MS Sans Serif", "DejaVu Sans"},
        {"Trebuchet MS", "DejaVu Sans"}, {"Verdana", "DejaVu Sans"}, {"Arial", "Liberation Sans"},
        {"Courier New", "Liberation Mono"}, {"Consolas", "DejaVu Sans Mono"}, {"Lucida Console", "DejaVu Sans Mono"},
        {"Times New Roman", "Liberation Serif"}};
    return m.value(f, f);
#endif
}

static constexpr double kFloatScale = 1000.0;

double sliderValue(const QWidget* w) {
    auto* s = qobject_cast<const QSlider*>(w);
    if (!s) return 0;
    return w->property("vclMinF").isValid() ? s->value() / kFloatScale : s->value();
}

void setSliderValue(QWidget* w, double v) {
    auto* s = qobject_cast<QSlider*>(w);
    if (!s) return;
    s->setValue(int(std::lround(w->property("vclMinF").isValid() ? v * kFloatScale : v)));
}

QString delphiFormat(const QString& fmt, double value) {
    static const QRegularExpression re("%(-?)(\\d*)(?:\\.(\\d+))?([dusfnxDUSFNX])");
    auto m = re.match(fmt);
    if (!m.hasMatch()) return fmt;
    QChar t = m.captured(4).toLower()[0];
    int width = m.captured(2).toInt();
    QString v;
    if (t == 'd' || t == 'u') v = QString::number(qint64(std::llround(value)));
    else if (t == 'x') v = QString::number(qint64(std::llround(value)), 16).toUpper();
    else if (t == 'f' || t == 'n') v = QString::number(value, 'f', m.captured(3).isEmpty() ? 2 : m.captured(3).toInt());
    else v = QString::number(value);
    if (v.size() < width) v = m.captured(1).isEmpty() ? v.rightJustified(width) : v.leftJustified(width);
    return QString(fmt).replace(m.capturedStart(), m.capturedLength(), v);
}

// ------------------------------------------------------------------ layout

class AlignFilter : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Resize || e->type() == QEvent::LayoutRequest) relayout(static_cast<QWidget*>(o));
        return false;
    }
};

static AlignFilter* filter() {
    static AlignFilter* f = new AlignFilter;
    return f;
}

static QRect clientRect(QWidget* c) {
    QRect r = c->rect();
    QString cls = str(c, "vclClass");
    if (cls == "TPanel") {
        int b = 0;
        if (str(c, "vclBevelOuter") != "bvNone") ++b;
        if (!str(c, "vclBevelInner").isEmpty() && str(c, "vclBevelInner") != "bvNone") ++b;
        r.adjust(b, b, -b, -b);
    } else if (auto* gb = qobject_cast<QGroupBox*>(c)) {
        r = gb->contentsRect();
    }
    return r;
}

// The whole text of a label or button, also when elideLongTexts() shows it shortened.
static QString fullText(const QWidget* w, const QString& current) {
    QVariant shown = w->property("elShown");
    return shown.isValid() && shown.toString() == current ? w->property("elFull").toString() : current;
}

// TLabel.AutoSize (AdjustBounds): a label is as wide as its text, so a longer text - another
// font, a translation - isn't cut off. Left aligned labels grow to the right, right aligned ones
// to the left, centered ones both ways.
// The free space beside a control, up to the next control on the same row or the container's edge:
// to the right of its left edge, or to the left of its right edge.
static int freeRight(QWidget* w) {
    QRect g = w->geometry();
    int right = clientRect(w->parentWidget()).right();
    for (QObject* o : w->parentWidget()->children()) {
        auto* s = qobject_cast<QWidget*>(o);
        if (!s || s == w || s->isWindow() || !str(s, "vclAlign").isEmpty()) continue;
        QRect r = s->geometry();
        if (r.left() > g.left() + 2 && r.top() < g.bottom() && r.bottom() > g.top()) right = std::min(right, r.left() - 3);
    }
    return right;
}

static int freeLeft(QWidget* w) {
    QRect g = w->geometry();
    int left = clientRect(w->parentWidget()).left();
    for (QObject* o : w->parentWidget()->children()) {
        auto* s = qobject_cast<QWidget*>(o);
        if (!s || s == w || s->isWindow() || !str(s, "vclAlign").isEmpty()) continue;
        QRect r = s->geometry();
        if (r.right() < g.right() - 2 && r.top() < g.bottom() && r.bottom() > g.top()) left = std::max(left, r.right() + 3);
    }
    return left;
}

static void autoSize(QLabel* l) {
    if (l->wordWrap() || !l->pixmap().isNull() || !str(l, "vclAlign").isEmpty() || !l->parentWidget()) return;
    QFontMetrics fm(l->font());
    int text = 0;
    QString full = fullText(l, l->text());
    for (const QString& line : full.split('\n')) text = std::max(text, fm.horizontalAdvance(line));
    QMargins m = l->contentsMargins();
    int w = text + m.left() + m.right() + 2 * l->margin() + 2; // + the shadow
    if (full.isEmpty()) return;
    if (!l->property("vclOrigW").isValid()) l->setProperty("vclOrigW", l->property("vclDesignW").toInt());
    int orig = l->property("vclOrigW").toInt();
    QRect g = l->geometry();
    Qt::Alignment a = l->alignment();
    // Don't grow over the neighbors: a text that still doesn't fit is shortened with "…"
    // (elideLongTexts), never drawn under another control.
    if (w > orig) {
        int room;
        if (a & Qt::AlignRight) room = g.right() - freeLeft(l) + 1;
        else if (a & Qt::AlignHCenter) room = g.width() + 2 * std::min(freeRight(l) - g.right(), g.left() - freeLeft(l));
        else room = freeRight(l) - g.left() + 1;
        w = std::min(w, std::max(room, orig));
    }
    if (w == g.width()) return;
    int x = g.x();
    if (a & Qt::AlignRight) x = g.right() + 1 - w;
    else if (a & Qt::AlignHCenter) x = g.x() + (g.width() - w) / 2;
    l->setGeometry(x, g.y(), w, g.height());
    l->setProperty("vclDesignX", l->property("vclDesignX").toInt() + (x - g.x()));
    l->setProperty("vclDesignW", w);
}

// Check boxes and radio buttons with a longer text (a translation, another font) take the free
// space to their right, up to the next control on the same row or the edge of their container.
// They never get narrower than in the design. Ones with the box on the right are left alone.
static void growButton(QAbstractButton* b) {
    if (b->text().isEmpty() || b->property("vclRightToLeft").toBool()) return;
    QString an = str(b, "vclAnchors");
    if (an.contains("akRight") && !an.contains("akLeft")) return;
    QWidget* p = b->parentWidget();
    if (!p) return;
    if (!b->property("vclOrigW").isValid()) b->setProperty("vclOrigW", b->property("vclDesignW").toInt());
    int orig = b->property("vclOrigW").toInt();
    int need = b->sizeHint().width();
    if (QString full = fullText(b, b->text()); full != b->text()) {
        QFontMetrics fm(b->font());
        auto width = [&fm](QString t) {
            t.remove(QRegularExpression("&(?!&)"));
            int m = 0;
            for (const QString& line : t.split('\n')) m = std::max(m, fm.horizontalAdvance(line));
            return m;
        };
        need += width(full) - width(b->text());
    }
    QRect g = b->geometry();
    int w = orig;
    if (need > orig) {
        int right = freeRight(b);
        w = std::max(orig, std::min(need, right - g.left() + 1));
    }
    if (w == g.width()) return;
    b->resize(w, g.height());
    b->setProperty("vclDesignW", w);
}

void relayout(QWidget* c) {
    if (!c || laidOut(c)) return; // (its layout places its controls)
    QRect client = clientRect(c);
    QList<QWidget*> kids;
    for (QObject* o : c->children())
        if (auto* w = qobject_cast<QWidget*>(o); w && !w->isWindow() && w->property("vclDesignW").isValid())
            kids.append(w);
    for (QWidget* w : kids)
        if (auto* l = qobject_cast<QLabel*>(w); l && l->property("vclAutoSize").toBool()) autoSize(l);
    for (QWidget* w : kids)
        if (qobject_cast<QCheckBox*>(w) || qobject_cast<QRadioButton*>(w)) growButton(static_cast<QAbstractButton*>(w));
    auto aligned = [&](const char* al) {
        QList<QWidget*> v;
        for (QWidget* w : kids)
            if (!w->isHidden() && str(w, "vclAlign") == al) v.append(w);
        return v;
    };
    // VCL order: top, bottom, left, right, client - each sorted by position.
    auto tops = aligned("alTop");
    std::stable_sort(tops.begin(), tops.end(), [](QWidget* a, QWidget* b) { return a->y() < b->y(); });
    for (QWidget* w : tops) {
        w->setGeometry(client.left(), client.top(), client.width(), w->height());
        client.setTop(client.top() + w->height());
    }
    auto bottoms = aligned("alBottom");
    std::stable_sort(bottoms.begin(), bottoms.end(), [](QWidget* a, QWidget* b) { return a->y() > b->y(); });
    for (QWidget* w : bottoms) {
        w->setGeometry(client.left(), client.bottom() + 1 - w->height(), client.width(), w->height());
        client.setBottom(client.bottom() - w->height());
    }
    auto lefts = aligned("alLeft");
    std::stable_sort(lefts.begin(), lefts.end(), [](QWidget* a, QWidget* b) { return a->x() < b->x(); });
    for (QWidget* w : lefts) {
        w->setGeometry(client.left(), client.top(), w->width(), client.height());
        client.setLeft(client.left() + w->width());
    }
    auto rights = aligned("alRight");
    std::stable_sort(rights.begin(), rights.end(), [](QWidget* a, QWidget* b) { return a->x() > b->x(); });
    for (QWidget* w : rights) {
        w->setGeometry(client.right() + 1 - w->width(), client.top(), w->width(), client.height());
        client.setRight(client.right() - w->width());
    }
    for (QWidget* w : aligned("alClient")) w->setGeometry(client);

    // Anchors for non-aligned controls, relative to the parent's design size.
    int pw0 = c->property("vclDesignCW").isValid() ? c->property("vclDesignCW").toInt() : c->width();
    int ph0 = c->property("vclDesignCH").isValid() ? c->property("vclDesignCH").toInt() : c->height();
    int dw = c->width() - pw0, dh = c->height() - ph0;
    for (QWidget* w : kids) {
        QString al = str(w, "vclAlign");
        if (!al.isEmpty() && al != "alNone") continue;
        QString an = str(w, "vclAnchors");
        if (an.isEmpty()) an = "akLeft,akTop";
        if (an == "akLeft,akTop" && dw == 0 && dh == 0) continue;
        bool l = an.contains("akLeft"), r = an.contains("akRight"), t = an.contains("akTop"), b = an.contains("akBottom");
        int x0 = w->property("vclDesignX").toInt(), y0 = w->property("vclDesignY").toInt();
        int w0 = w->property("vclDesignW").toInt(), h0 = w->property("vclDesignH").toInt();
        int x = x0, y = y0, ww = w0, hh = h0;
        if (l && r) ww = w0 + dw;
        else if (r) x = x0 + dw;
        else if (!l) x = x0 + dw / 2;
        if (t && b) hh = h0 + dh;
        else if (b) y = y0 + dh;
        else if (!t) y = y0 + dh / 2;
        w->setGeometry(x, y, std::max(0, ww), std::max(0, hh));
    }
}

static void rememberDesign(QWidget* w) {
    w->setProperty("vclDesignX", w->x());
    w->setProperty("vclDesignY", w->y());
    w->setProperty("vclDesignW", w->width());
    w->setProperty("vclDesignH", w->height());
    w->setProperty("vclDesignCW", w->width());
    w->setProperty("vclDesignCH", w->height());
}

void place(QWidget* w, const QRect& r) {
    w->setGeometry(r);
    rememberDesign(w);
    // The container's anchor reference is its current size from now on, too.
    if (QWidget* p = w->parentWidget()) {
        p->setProperty("vclDesignCW", p->width());
        p->setProperty("vclDesignCH", p->height());
        // ...and its other children keep their current positions as reference.
        for (QObject* o : p->children())
            if (auto* k = qobject_cast<QWidget*>(o); k && k != w && k->property("vclDesignX").isValid() &&
                                                    str(k, "vclAlign").isEmpty())
                rememberDesign(k);
    }
}

void reparent(QWidget* w, QWidget* newParent, const QPoint& pos) {
    bool hidden = w->isHidden();
    w->setParent(newParent);
    place(w, QRect(pos, w->size()));
    if (!hidden) w->show();
    if (!newParent->property("vclHasFilter").toBool()) {
        newParent->installEventFilter(filter());
        newParent->setProperty("vclHasFilter", true);
    }
}

void setVisible(QWidget* w, bool visible) {
    w->setVisible(visible);
    if (w->parentWidget()) relayout(w->parentWidget());
}


// ------------------------------------------------------------------ elide

// The safety net for texts that still don't fit (a long translation, a font missing on this
// system): the text is cut with "…" and the whole text shows as the tooltip. A text set later by
// the program is picked up the next time the control is painted or resized.
namespace {

QString widgetText(QWidget* w) {
    if (auto* l = qobject_cast<QLabel*>(w)) return l->text();
    return static_cast<QAbstractButton*>(w)->text();
}

void setWidgetText(QWidget* w, const QString& t) {
    if (auto* l = qobject_cast<QLabel*>(w)) l->setText(t);
    else static_cast<QAbstractButton*>(w)->setText(t);
}

// The text as shown: buttons (and labels with a buddy) hide the & of a keyboard shortcut.
QString visibleText(QWidget* w, const QString& t) {
    auto* l = qobject_cast<QLabel*>(w);
    if (l && !l->buddy()) return t;
    QString out;
    for (int i = 0; i < t.size(); ++i) {
        if (t[i] == '&' && i + 1 < t.size()) ++i;
        out += t[i];
    }
    return out;
}

int ownRoom(QWidget* w);

// Room for the text: what the control has, less any part of it outside its container.
int textRoom(QWidget* w) {
    int room = ownRoom(w);
    if (QWidget* p = w->parentWidget()) {
        int hidden = w->geometry().right() - p->contentsRect().right();
        if (hidden > 0) room -= hidden;
    }
    return room;
}

int ownRoom(QWidget* w) {
    if (auto* l = qobject_cast<QLabel*>(w)) return l->contentsRect().width() - 2 * l->margin() - l->indent() * (l->indent() > 0);
    auto* b = static_cast<QAbstractButton*>(w);
    QStyleOptionButton opt;
    opt.initFrom(b);
    opt.text = b->text();
    opt.icon = b->icon();
    opt.iconSize = b->iconSize();
    if (qobject_cast<QCheckBox*>(b)) return b->style()->subElementRect(QStyle::SE_CheckBoxContents, &opt, b).width() - 2;
    if (qobject_cast<QRadioButton*>(b)) return b->style()->subElementRect(QStyle::SE_RadioButtonContents, &opt, b).width() - 2;
    int room = b->style()->subElementRect(QStyle::SE_PushButtonContents, &opt, b).width() - 4;
    if (!b->icon().isNull()) room -= b->iconSize().width() + 4;
    return room;
}

class ElideFilter : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* o, QEvent* e) override {
        switch (e->type()) {
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Paint:
        case QEvent::FontChange:
        case QEvent::StyleChange:
            update(static_cast<QWidget*>(o), e->type() == QEvent::Paint);
            break;
        default:
            break;
        }
        return false;
    }

private:
    void update(QWidget* w, bool painting) {
        if (busy_) return;
        if (auto* l = qobject_cast<QLabel*>(w);
            l && (l->wordWrap() || !l->pixmap().isNull() || l->textFormat() == Qt::RichText ||
                  (l->textFormat() == Qt::AutoText && Qt::mightBeRichText(l->text()))))
            return; // made into an image or rich text by the program
        QString current = widgetText(w);
        QVariant shown = w->property("elShown");
        bool changed = !shown.isValid() || current != shown.toString();
        if (!changed && painting && w->property("elWidth").toInt() == w->width()) return;
        QString full = changed ? current : w->property("elFull").toString();
        w->setProperty("elFull", full);
        w->setProperty("elWidth", w->width());
        QFontMetrics fm(w->font());
        int room = textRoom(w);
        QString visible = visibleText(w, full);
        bool fits = true;
        QStringList lines = visible.split('\n');
        for (const QString& line : lines) // (no text, as an icon-only button: nothing to cut short)
            if (!line.isEmpty() && fm.horizontalAdvance(line) > room) fits = false;
        QString text = full;
        if (!fits && room > 0) {
            for (QString& line : lines) line = fm.elidedText(line, Qt::ElideRight, room);
            text = lines.join('\n');
            if (qobject_cast<QAbstractButton*>(w)) text.replace('&', "&&");
        }
        bool tip = w->property("elTip").toBool();
        if (!fits && !tip) {
            w->setProperty("elOwnTip", w->toolTip());
            QString own = w->toolTip();
            w->setToolTip(own.isEmpty() ? visible : visible + "\n\n" + own);
            w->setProperty("elTip", true);
        } else if (fits && tip) {
            w->setToolTip(w->property("elOwnTip").toString());
            w->setProperty("elTip", false);
        } else if (!fits && tip && changed) {
            QString own = w->property("elOwnTip").toString();
            w->setToolTip(own.isEmpty() ? visible : visible + "\n\n" + own);
        }
        w->setProperty("elShown", text);
        if (text != current) {
            busy_ = true;
            setWidgetText(w, text);
            busy_ = false;
        }
    }
    bool busy_ = false;
};

// A drop-down list opens as wide as its longest entry (the box itself keeps its design width)
class ComboWidener : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::KeyPress || e->type() == QEvent::Wheel)
            if (auto* c = qobject_cast<QComboBox*>(o); c && c->view()) {
                const int widest = c->view()->sizeHintForColumn(0) + c->style()->pixelMetric(QStyle::PM_ScrollBarExtent) + 8;
                c->view()->setMinimumWidth(std::max(c->width(), widest));
            }
        return false;
    }
};

ComboWidener* comboWidener() {
    static ComboWidener* f = new ComboWidener;
    return f;
}

ElideFilter* elideFilter() {
    static ElideFilter* f = new ElideFilter;
    return f;
}

bool elidable(QWidget* w) {
    if (auto* l = qobject_cast<QLabel*>(w))
        return !l->wordWrap() && l->pixmap().isNull() && l->textFormat() != Qt::RichText &&
               !(l->textFormat() == Qt::AutoText && Qt::mightBeRichText(l->text()));
    if (qobject_cast<QCheckBox*>(w) || qobject_cast<QRadioButton*>(w)) return true;
    if (qobject_cast<QPushButton*>(w)) {
        // Tool bar buttons show only their icon; their text becomes the tooltip.
        for (QWidget* p = w->parentWidget(); p; p = p->parentWidget())
            if (p->objectName() == "ToolBarButtons" || p->objectName() == "ImagesToolBarButtons") return false;
        return true;
    }
    return false;
}

} // namespace

void elideLongTexts(QWidget* w) {
    if (!w->property("elInstalled").toBool() && elidable(w)) {
        w->setProperty("elInstalled", true);
        w->installEventFilter(elideFilter());
    }
}

void stopEliding(QWidget* w) {
    if (!w->property("elInstalled").toBool()) return;
    w->removeEventFilter(elideFilter());
    w->setProperty("elInstalled", false);
    const QString current = widgetText(w);
    const QVariant shown = w->property("elShown");
    if (shown.isValid() && shown.toString() == current && current != w->property("elFull").toString())
        setWidgetText(w, w->property("elFull").toString());
    if (w->property("elTip").toBool()) w->setToolTip(w->property("elOwnTip").toString());
    for (const char* p : {"elShown", "elFull", "elWidth", "elTip", "elOwnTip"}) w->setProperty(p, QVariant());
}

void loose(QWidget* w) {
    stopEliding(w);
    w->setMinimumSize(0, 0);
    w->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
}

QPushButton* textButton(QPushButton* b) {
    const int design = b->width();
    loose(b);
    b->setMinimumWidth(std::max(design, b->fontMetrics().horizontalAdvance(b->text().remove('&')) + 16));
    b->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return b;
}

QBoxLayout* boxed(QBoxLayout* l, int left, int top, int right, int bottom, int spacing) {
    l->setContentsMargins(left, top, right, bottom);
    l->setSpacing(spacing);
    return l;
}

QWidget* replaceDesigned(QWidget* designed, QWidget* made) {
    if (made->parentWidget() != designed->parentWidget()) made->setParent(designed->parentWidget());
    for (const QByteArray& name : designed->dynamicPropertyNames())
        if (name.startsWith("vcl")) made->setProperty(name, designed->property(name));
    made->setGeometry(designed->geometry());
    made->stackUnder(designed);
    made->show();
    delete designed;
    return made;
}

// ------------------------------------------------------------------- apply

// vclColor / vclFontColor as the widget's palette (the night colors in Night Mode)
static void applyColors(QWidget* form, QWidget* w) {
        QColor bg = color(w, "vclColor"), fg = color(w, "vclFontColor");

        if (bg.isValid() || fg.isValid()) {
            QPalette pal = w->palette();
            bool isInput = qobject_cast<QLineEdit*>(w) || qobject_cast<QComboBox*>(w) ||
                           qobject_cast<QAbstractItemView*>(w) || qobject_cast<QPlainTextEdit*>(w) ||
                           qobject_cast<QTextEdit*>(w);
            if (bg.isValid()) {
                if (isInput) pal.setColor(QPalette::Base, bg);
                else if (w == form || qobject_cast<QGroupBox*>(w) || (qobject_cast<QLabel*>(w) && !qobject_cast<ElImage*>(w) &&
                                                                       str(w, "vclTransparent") != "True" && str(w, "vclClass") == "TLabel")) {
                    pal.setColor(QPalette::Window, bg);
                    w->setAutoFillBackground(true);
                } else if (qobject_cast<ElImage*>(w)) {
                    pal.setColor(QPalette::Window, bg);
                    w->setAutoFillBackground(true);
                }
            }
            if (fg.isValid()) {
                pal.setColor(QPalette::WindowText, fg);
                pal.setColor(QPalette::Text, fg);
                pal.setColor(QPalette::ButtonText, fg);
            }
            w->setPalette(pal);
        }

}

void recolor(QWidget* form) {
    QList<QWidget*> all = form->findChildren<QWidget*>();
    all.prepend(form);
    for (QWidget* w : all) {
        if (w->property("vclColor").isValid() || w->property("vclFontColor").isValid()) applyColors(form, w);
        w->update();
    }
}

void apply(QWidget* form) {
    QList<QWidget*> all{form};
    all += form->findChildren<QWidget*>();
    QHash<QString, QButtonGroup*> groups;

    for (QWidget* w : all) {
        // Design geometry (anchors are relative to it) and design client size for containers.
        w->setProperty("vclDesignX", w->x());
        w->setProperty("vclDesignY", w->y());
        w->setProperty("vclDesignW", w->width());
        w->setProperty("vclDesignH", w->height());
        w->setProperty("vclDesignCW", w->width());
        w->setProperty("vclDesignCH", w->height());

        // Fonts
        QString fam = str(w, "vclFontName");
        QString px = str(w, "vclFontPx");
        QString style = str(w, "vclFontStyle");
        if (!fam.isEmpty() || !px.isEmpty() || !style.isEmpty()) {
            QFont f = w->font();
            if (!fam.isEmpty()) // the language's font in place of the usual interface fonts
                f.setFamily(!ScriptFonts::uiFamily().isEmpty() && ScriptFonts::isInterfaceFamily(fam) ? ScriptFonts::uiFamily()
                                                                                                    : mapFamily(fam));
            if (!px.isEmpty()) f.setPixelSize(ScriptFonts::shownPixels(px.toInt())); // (+ the language's FontSize=)
            f.setBold(style.contains("fsBold"));
            f.setItalic(style.contains("fsItalic"));
            f.setUnderline(style.contains("fsUnderline"));
            f.setStrikeOut(style.contains("fsStrikeOut"));
            // (most controls have their container's font already: setting it again re-checks
            // every control inside, which made windows of hundreds of controls slow to build)
            if (f != w->font()) w->setFont(f);
        }

        // Colors
        applyColors(form, w);

        // Label alignment
        if (auto* lbl = qobject_cast<QLabel*>(w)) {
            Qt::Alignment a = Qt::AlignLeft;
            QString ta = str(w, "vclAlignment");
            if (ta == "taCenter") a = Qt::AlignHCenter;
            else if (ta == "taRightJustify") a = Qt::AlignRight;
            QString lay = str(w, "vclLayout");
            a |= lay == "tlCenter" ? Qt::AlignVCenter : lay == "tlBottom" ? Qt::AlignBottom : Qt::AlignTop;
            if (!qobject_cast<ElImage*>(w)) lbl->setAlignment(a);
        }
        if (auto* le = qobject_cast<QLineEdit*>(w)) {
            QString ta = str(w, "vclAlignment");
            if (ta == "taCenter") le->setAlignment(Qt::AlignCenter);
            else if (ta == "taRightJustify") le->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        }

        // Grouped speed buttons (GroupIndex) are exclusive within their parent.
        if (auto* b = qobject_cast<QAbstractButton*>(w); b && w->property("vclGroupIndex").isValid()) {
            QString key = QString::number(quintptr(w->parentWidget())) + ":" + str(w, "vclGroupIndex");
            QButtonGroup*& g = groups[key];
            if (!g) {
                g = new QButtonGroup(w->parentWidget());
                g->setExclusive(str(w, "vclAllowAllUp") != "True");
            }
            g->addButton(b);
        }

        // Float sliders, and "<Slider>Label" captions formatted from the label's hint (as the Delphi
        // OnChange handlers do: Label.Caption := Format(Label.Hint, [Slider.Position])).
        if (auto* sl = qobject_cast<QSlider*>(w)) {
            if (w->property("vclMinF").isValid()) {
                sl->setRange(int(std::lround(w->property("vclMinF").toString().toDouble() * kFloatScale)),
                             int(std::lround(w->property("vclMaxF").toString().toDouble() * kFloatScale)));
                sl->setSingleStep(10);
                sl->setPageStep(100);
                setSliderValue(w, w->property("vclPosF").toString().toDouble());
            }
            if (auto* lbl = form->findChild<QLabel*>(w->objectName() + "Label"); lbl && lbl->toolTip().contains('%')) {
                QString fmt = lbl->toolTip();
                auto update = [sl, lbl, fmt] { lbl->setText(delphiFormat(fmt, sliderValue(sl))); };
                QObject::connect(sl, &QSlider::valueChanged, lbl, update);
                update();
            }
        }

        if (auto* sp = qobject_cast<ElSplitter*>(w)) {
            QString al = str(w, "vclAlign");
            sp->setCursor((al == "alLeft" || al == "alRight") ? Qt::SplitHCursor : Qt::SplitVCursor);
        }
    }

    for (QWidget* w : all)
        if (w != form) elideLongTexts(w);
    for (QWidget* w : all)
        if (qobject_cast<QComboBox*>(w)) w->installEventFilter(comboWidener());

    // Visibility last (hiding doesn't disturb design geometry), then layout.
    for (QWidget* w : all)
        if (w != form && w->property("vclVisible").isValid() && !w->property("vclVisible").toBool()) w->hide();
    for (QWidget* w : all) {
        bool hasKids = false;
        for (QObject* o : w->children())
            if (auto* k = qobject_cast<QWidget*>(o); k && k->property("vclClass").isValid()) { hasKids = true; break; }
        if (hasKids) {
            w->installEventFilter(filter());
            relayout(w);
        }
    }
}

// ------------------------------------------------------------ autoLayout

namespace {

QRect designRect(const QWidget* w) {
    return QRect(w->property("vclDesignX").toInt(), w->property("vclDesignY").toInt(), w->property("vclDesignW").toInt(),
                 w->property("vclDesignH").toInt());
}

// A control of the converted form (not a part of one, such as a list's scroll bars)
bool hasDesign(const QWidget* w) { return w->property("vclDesignW").isValid() && w->property("vclClass").isValid(); }

// A control whose parts aren't controls of the form: placed whole
bool single(const QWidget* w) {
    return qobject_cast<const QAbstractScrollArea*>(w) || qobject_cast<const QComboBox*>(w) ||
           qobject_cast<const QAbstractSpinBox*>(w) || qobject_cast<const QAbstractButton*>(w) ||
           qobject_cast<const QLabel*>(w) || qobject_cast<const QLineEdit*>(w) || qobject_cast<const QSlider*>(w);
}

// Controls that overlap in the design (one shown in place of the other): kept at their design
// places, the spot as big as all of them.
class FixedSpot : public QLayout {
public:
    explicit FixedSpot(const QList<QWidget*>& ws) {
        QRect all;
        for (QWidget* w : ws) all |= designRect(w);
        origin_ = all.topLeft();
        for (QWidget* w : ws) {
            addChildWidget(w);
            items_.append({new QWidgetItem(w), designRect(w).topLeft() - origin_});
        }
    }
    ~FixedSpot() override {
        for (auto& i : items_) delete i.item;
    }
    void addItem(QLayoutItem* item) override { items_.append({item, QPoint()}); }
    int count() const override { return int(items_.size()); }
    QLayoutItem* itemAt(int i) const override { return i >= 0 && i < items_.size() ? items_[i].item : nullptr; }
    QLayoutItem* takeAt(int i) override {
        if (i < 0 || i >= items_.size()) return nullptr;
        return items_.takeAt(i).item;
    }
    QSize sizeHint() const override {
        QRect r;
        for (const auto& i : items_) r |= QRect(i.at, size(i.item));
        return r.size();
    }
    QSize minimumSize() const override { return sizeHint(); }
    void setGeometry(const QRect& r) override {
        QLayout::setGeometry(r);
        for (const auto& i : items_) i.item->setGeometry(QRect(r.topLeft() + i.at, size(i.item)));
    }

private:
    static QSize size(QLayoutItem* it) {
        return it->widget() ? it->widget()->size().expandedTo(it->sizeHint()).expandedTo(it->minimumSize()) : it->sizeHint();
    }
    struct Item {
        QLayoutItem* item;
        QPoint at;
    };
    QList<Item> items_;
    QPoint origin_;
};

// A group's title bar with controls on it by design (a check box, a note): the bar behind, the
// controls laid out over it
class BehindLayout : public QLayout {
public:
    BehindLayout(QWidget* back, QLayout* front, int backTop) : back_(new QWidgetItem(back)), top_(backTop) {
        addChildWidget(back);
        addChildLayout(front);
        front_ = front;
    }
    ~BehindLayout() override {
        delete back_;
        delete front_;
    }
    void addItem(QLayoutItem*) override {}
    int count() const override { return 2; }
    QLayoutItem* itemAt(int i) const override { return i == 0 ? back_ : i == 1 ? static_cast<QLayoutItem*>(front_) : nullptr; }
    QLayoutItem* takeAt(int i) override {
        QLayoutItem* it = itemAt(i);
        if (i == 0) back_ = nullptr;
        if (i == 1) front_ = nullptr;
        return it;
    }
    QSize sizeHint() const override { return both(&QLayoutItem::sizeHint); }
    QSize minimumSize() const override { return both(&QLayoutItem::minimumSize); }
    Qt::Orientations expandingDirections() const override { return Qt::Horizontal; }
    void setGeometry(const QRect& r) override {
        QLayout::setGeometry(r);
        if (back_) back_->setGeometry(QRect(r.left(), r.top() + top_, r.width(), back_->sizeHint().height()));
        if (front_) front_->setGeometry(r);
        if (back_ && back_->widget()) back_->widget()->lower(); // (behind the controls on it)
    }

private:
    QSize both(QSize (QLayoutItem::*f)() const) const {
        QSize sz;
        if (back_) sz = sz.expandedTo((back_->*f)() + QSize(0, top_));
        if (front_) sz = sz.expandedTo((front_->*f)());
        return sz;
    }
    QLayoutItem* back_;
    QLayout* front_ = nullptr;
    int top_;
};

// A cover over its parent's controls: placed with the design's margins whenever the parent's size changes
class OverlayFilter : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Resize || e->type() == QEvent::LayoutRequest || e->type() == QEvent::Show)
            if (auto* p = qobject_cast<QWidget*>(o))
                for (QObject* k : p->children())
                    if (auto* w = qobject_cast<QWidget*>(k); w && w->property("vclOverlay").toBool()) {
                        const QMargins m = w->property("vclOverlayMargins").value<QMargins>();
                        QRect r = p->rect().marginsRemoved(m);
                        // and over every control under it (laid out a pixel or two wider than designed)
                        for (QObject* o2 : p->children())
                            if (auto* s = qobject_cast<QWidget*>(o2); s && s != w && !s->isHidden() && s->y() >= r.top())
                                r |= s->geometry();
                        w->setGeometry(r & p->rect());
                        w->raise();
                    }
        return false;
    }
};

OverlayFilter* overlayFilter() {
    static OverlayFilter* f = new OverlayFilter;
    return f;
}

bool anchoredRight(const QWidget* w) {
    const QString an = str(w, "vclAnchors");
    return an.contains("akRight") && !an.contains("akLeft");
}

// A control as a layout item: its design size, more when its text (a translation, a larger font)
// needs it. Fields (edit boxes, lists, sliders) keep their design width.
void prepareLeaf(QWidget* w) {
    if (w->property("vclDataLabel").toBool()) { // data: its design width, shortened when long
        const QRect d = designRect(w);
        w->setMinimumSize(d.width(), d.height());
        w->setMaximumSize(QWIDGETSIZE_MAX, d.height());
        w->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        return;
    }
    stopEliding(w);
    const QRect d = designRect(w);
    const QFontMetrics fm(w->font());
    int width = d.width(), height = d.height();
    QString text;
    if (qobject_cast<QLabel*>(w) || qobject_cast<QAbstractButton*>(w)) text = widgetText(w).remove('&');
    int textWidth = 0;
    for (const QString& line : text.split('\n')) textWidth = std::max(textWidth, fm.horizontalAdvance(line));
    const int lines = text.isEmpty() ? 1 : int(text.count('\n')) + 1;
    if (auto* l = qobject_cast<QLabel*>(w); l && l->wordWrap()) {
        height = std::max(height, l->heightForWidth(width)); // (as many lines as its text needs)
    } else if (auto* l = qobject_cast<QLabel*>(w); l && l->pixmap().isNull() &&
               (l->textFormat() == Qt::RichText || (l->textFormat() == Qt::AutoText && Qt::mightBeRichText(l->text())))) {
        width = std::max(width, l->sizeHint().width()); // (rich text: as laid out, not its markup)
        height = std::max(height, l->sizeHint().height());
    } else if (auto* l = qobject_cast<QLabel*>(w); l && l->pixmap().isNull()) {
        width = std::max(width, textWidth + 2);
        height = std::max(height, fm.height() * lines);
    } else if (qobject_cast<QCheckBox*>(w) || qobject_cast<QRadioButton*>(w)) {
        width = std::max(width, w->sizeHint().width());
        height = std::max(height, fm.height() + 2);
    } else if (qobject_cast<QPushButton*>(w)) {
        width = std::max(width, textWidth + 8);
        height = std::max(height, fm.height() * lines + 6);
    } else if (qobject_cast<QLineEdit*>(w) || qobject_cast<QComboBox*>(w) || qobject_cast<QAbstractSpinBox*>(w)) {
        height = std::max(height, fm.height() + 6);
    }
    w->setMinimumSize(std::max(0, width), std::max(0, height));
    if (qobject_cast<QLabel*>(w) || qobject_cast<QCheckBox*>(w) || qobject_cast<QRadioButton*>(w)) {
        // a text the program sets later (a longer one) still gets its room: as wide as its text
        w->setMaximumSize(QWIDGETSIZE_MAX, std::max(0, height));
        w->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    } else {
        w->setMaximumSize(std::max(0, width), std::max(0, height)); // (wider only across a whole row: stack())
        w->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
}

QLayout* cut(const QList<QWidget*>& ws, const QRect& box, int depth);

// Items one under the other (rows), or side by side (columns): the design's gaps between them,
// and each one as far from the other edge as designed (a row's indent, a column's top)
QLayout* stack(const QList<QList<QWidget*>>& parts, Qt::Orientation o, const QRect& box, int depth) {
    const bool vertical = o == Qt::Vertical;
    auto* l = new QBoxLayout(vertical ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    l->setSpacing(0);
    l->setContentsMargins(0, 0, 0, 0);
    int end = vertical ? box.top() : box.left();
    bool stretched = false;
    for (const QList<QWidget*>& part : parts) {
        QRect r;
        for (QWidget* w : part) r |= designRect(w);
        const int start = vertical ? r.top() : r.left();
        // right-anchored controls at the end of a row: they stay at the right edge
        bool right = !vertical && !stretched;
        for (QWidget* w : part) right = right && anchoredRight(w);
        if (right) {
            l->addStretch(1);
            l->addSpacing(std::max(0, start - end));
            stretched = true;
        } else if (start > end) {
            l->addSpacing(start - end);
        }
        // the part, as far from the other edge as designed
        auto* cell = new QBoxLayout(vertical ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
        cell->setSpacing(0);
        cell->setContentsMargins(0, 0, 0, 0);
        const int offset = vertical ? r.left() - box.left() : r.top() - box.top();
        // a single control across the whole width: it fills the width
        const bool across = vertical && part.size() == 1 && r.left() - box.left() <= 2 && box.right() - r.right() <= 3;
        bool bottomed = false;
        if (offset > 0 && !across) cell->addSpacing(offset);
        if (part.size() == 1) {
            QWidget* w = part.first();
            if (across) {
                QSizePolicy sp = w->sizePolicy();
                sp.setHorizontalPolicy(QSizePolicy::Expanding);
                w->setSizePolicy(sp);
                w->setMaximumWidth(QWIDGETSIZE_MAX);
                cell->addWidget(w);
            } else {
                // side by side: a short control (a label beside a field) centered on the row
                const bool middle = !vertical && r.height() <= 30;
                if (!vertical && w->property("vclRowBottom").toBool()) {
                    cell->insertStretch(0, 1); // (at the bottom of its row)
                    cell->addWidget(w, 0);
                    bottomed = true;
                } else {
                    cell->addWidget(w, 0, middle ? Qt::AlignVCenter : Qt::Alignment());
                }
            }
        } else {
            cell->addLayout(cut(part, r, depth + 1));
        }
        if (!across && !bottomed) cell->addStretch(1);
        l->addLayout(cell);
        end = vertical ? r.bottom() + 1 : r.right() + 1;
    }
    if (!stretched) l->addStretch(1);
    return l;
}

// Split along the gaps of the design: first into rows (a row: what overlaps vertically), else into
// columns; what overlaps both ways keeps its design places.
QList<QList<QWidget*>> split(QList<QWidget*> ws, Qt::Orientation o) {
    std::stable_sort(ws.begin(), ws.end(), [o](QWidget* a, QWidget* b) {
        return o == Qt::Vertical ? designRect(a).top() < designRect(b).top() : designRect(a).left() < designRect(b).left();
    });
    QList<QList<QWidget*>> parts;
    int reach = INT_MIN;
    for (QWidget* w : ws) {
        const QRect r = designRect(w);
        const int start = o == Qt::Vertical ? r.top() : r.left();
        const int stop = o == Qt::Vertical ? r.bottom() : r.right();
        if (parts.isEmpty() || start > reach - 4) parts.append(QList<QWidget*>()); // (4 pixels of overlap: design slips)
        parts.last().append(w);
        reach = std::max(reach, stop);
    }
    return parts;
}

// A panel of controls (a framed group), not a control
bool isPanel(const QWidget* w) {
    if (single(w)) return false;
    for (QObject* o : w->children())
        if (auto* g = qobject_cast<QWidget*>(o); g && !g->isWindow() && hasDesign(g)) return true;
    return false;
}

QLayout* cut(const QList<QWidget*>& ws, const QRect& box, int depth) {
    if (depth > 30) return new FixedSpot(ws);
    // Panels side by side: columns first, so a panel that grows moves its column only (rows
    // first would move the rest of its row). Controls: rows first, keeping a label with its field.
    bool panels = true;
    for (QWidget* w : ws) panels = panels && isPanel(w);
    const auto rows = split(ws, Qt::Vertical);
    const auto cols = split(ws, Qt::Horizontal);
    if (panels && cols.size() > 1) return stack(cols, Qt::Horizontal, box, depth);
    if (rows.size() > 1) return stack(rows, Qt::Vertical, box, depth);
    if (cols.size() > 1) return stack(cols, Qt::Horizontal, box, depth);
    // a title bar (a label across the whole width) with controls on it: the bar behind, the
    // controls laid out over it
    for (QWidget* bar : ws) {
        const QRect br = designRect(bar);
        if (!qobject_cast<QLabel*>(bar) || br.left() - box.left() > 2 || box.right() - br.right() > 3) continue;
        QList<QWidget*> rest = ws;
        rest.removeOne(bar);
        if (rest.isEmpty()) break;
        QSizePolicy sp = bar->sizePolicy();
        sp.setHorizontalPolicy(QSizePolicy::Expanding);
        bar->setSizePolicy(sp);
        bar->setMaximumWidth(QWIDGETSIZE_MAX);
        QRect restBox = box;
        QLayout* front = cut(rest, restBox, depth + 1);
        // the controls' own offset from the top: kept by a margin
        QRect rr;
        for (QWidget* w : rest) rr |= designRect(w);
        front->setContentsMargins(0, std::max(0, rr.top() - box.top()), 0, 0);
        return new BehindLayout(bar, front, std::max(0, br.top() - box.top()));
    }
    if (ws.size() == 1) { // (a single control)
        auto* l = new QBoxLayout(QBoxLayout::LeftToRight);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(ws.first());
        return l;
    }
    return new FixedSpot(ws);
}

} // namespace

bool laidOut(const QWidget* c) { return c && c->property("vclLaidOut").toBool(); }

void autoLayout(QWidget* c) {
    if (!c || laidOut(c) || c->layout()) return;
    QList<QWidget*> kids;
    bool overlays = false;
    for (QObject* o : c->children())
        if (auto* w = qobject_cast<QWidget*>(o); w && !w->isWindow() && hasDesign(w)) {
            if (w->property("vclOverlay").toBool()) { // (a cover: over the others, not among them)
                const QRect d = designRect(w);
                const int cw = c->property("vclDesignCW").isValid() ? c->property("vclDesignCW").toInt() : c->width();
                const int ch = c->property("vclDesignCH").isValid() ? c->property("vclDesignCH").toInt() : c->height();
                w->setProperty("vclOverlayMargins",
                               QVariant::fromValue(QMargins(d.left(), d.top(), std::max(0, cw - d.right() - 1),
                                                            std::max(0, ch - d.bottom() - 1))));
                autoLayout(w);
                overlays = true;
                continue;
            }
            kids.append(w);
        }
    if (overlays) c->installEventFilter(overlayFilter());
    // the panels inside first
    for (QWidget* k : kids) {
        bool parent = false;
        if (!single(k))
            for (QObject* o : k->children())
                if (auto* g = qobject_cast<QWidget*>(o); g && !g->isWindow() && hasDesign(g)) parent = true;
        if (parent) autoLayout(k);
        else prepareLeaf(k);
        if (parent) {
            const QRect d = designRect(k);
            k->setMinimumSize(d.size());
            k->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
        }
    }
    c->removeEventFilter(filter());
    c->setProperty("vclLaidOut", true);
    if (kids.isEmpty()) return;
    // the container's own room: its client area (a panel's bevel, a group box's frame)
    const QRect client = clientRect(c);
    QRect inner(0, 0, c->property("vclDesignCW").isValid() ? c->property("vclDesignCW").toInt() : c->width(),
                c->property("vclDesignCH").isValid() ? c->property("vclDesignCH").toInt() : c->height());
    QRect all;
    for (QWidget* k : kids) all |= designRect(k);
    QLayout* l = cut(kids, all, 0); // (inside the margins below)
    // the design's margins around the controls
    l->setContentsMargins(std::max(0, all.left() - client.left()), std::max(0, all.top() - client.top()),
                          std::max(0, inner.right() - all.right()), std::max(0, inner.bottom() - all.bottom()));
    c->setLayout(l);
}

void setOverlay(QWidget* w) {
    if (w) w->setProperty("vclOverlay", true);
}

void setRowBottom(QWidget* w) {
    if (w) w->setProperty("vclRowBottom", true);
}

void setDataLabel(QWidget* w) {
    if (w) w->setProperty("vclDataLabel", true);
}

void layoutForm(QWidget* form) {
    if (!form || laidOut(form) || form->layout()) return;
    const int fw = form->property("vclDesignCW").isValid() ? form->property("vclDesignCW").toInt() : form->width();
    const int fh = form->property("vclDesignCH").isValid() ? form->property("vclDesignCH").toInt() : form->height();
    QList<QWidget*> tops, bottoms, lefts, rights, clients, loose;
    for (QObject* o : form->children()) {
        auto* w = qobject_cast<QWidget*>(o);
        if (!w || w->isWindow() || !hasDesign(w)) continue;
        const QString al = str(w, "vclAlign");
        if (al == "alTop") tops << w;
        else if (al == "alBottom") bottoms << w;
        else if (al == "alLeft") lefts << w;
        else if (al == "alRight") rights << w;
        else if (al == "alClient") clients << w;
        else loose << w;
    }
    auto byY = [](QWidget* a, QWidget* b) { return designRect(a).top() < designRect(b).top(); };
    auto byX = [](QWidget* a, QWidget* b) { return designRect(a).left() < designRect(b).left(); };
    std::sort(tops.begin(), tops.end(), byY);
    std::sort(bottoms.begin(), bottoms.end(), byY);
    std::sort(lefts.begin(), lefts.end(), byX);
    std::sort(rights.begin(), rights.end(), byX);
    int top = 0, bottom = fh;
    for (QWidget* w : tops) if (w->isVisibleTo(form)) top = std::max(top, designRect(w).bottom() + 1);
    for (QWidget* w : bottoms) if (w->isVisibleTo(form)) bottom = std::min(bottom, designRect(w).top());

    // a docked bar: laid out itself; a bottom bar's buttons at its right end stay at the right
    auto bar = [&](QWidget* w, bool isBottom) {
        if (isBottom) {
            const QRect d = designRect(w);
            for (QObject* o : w->children())
                if (auto* k = qobject_cast<QWidget*>(o); k && hasDesign(k) && qobject_cast<QAbstractButton*>(k) &&
                                                         !qobject_cast<QCheckBox*>(k) && !qobject_cast<QRadioButton*>(k) &&
                                                         designRect(k).left() > d.width() / 2 && str(k, "vclAnchors").isEmpty())
                    k->setProperty("vclAnchors", "akTop,akRight");
        }
        bool parent = false;
        for (QObject* o : w->children())
            if (auto* k = qobject_cast<QWidget*>(o); k && !k->isWindow() && hasDesign(k)) parent = true;
        if (parent && !single(w)) autoLayout(w);
        else prepareLeaf(w);
        // (as wide as the window: not the design canvas's width, which can be far wider)
        if (QLayout* l = w->layout()) {
            QMargins m = l->contentsMargins();
            m.setRight(std::min(m.right(), std::max(m.left(), 8)));
            l->setContentsMargins(m);
        }
        w->setMinimumSize(0, designRect(w).height());
        w->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    };
    auto* lay = new QVBoxLayout;
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    for (QWidget* w : tops) {
        bar(w, false);
        lay->addWidget(w);
    }
    // the middle: the fill area, between the docked sides; the loose controls in a panel of their own
    auto* middle = new QHBoxLayout;
    middle->setContentsMargins(0, 0, 0, 0);
    middle->setSpacing(0);
    for (QWidget* w : lefts) {
        autoLayout(w);
        w->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        middle->addWidget(w);
    }
    auto* centre = new QVBoxLayout;
    centre->setContentsMargins(0, 0, 0, 0);
    centre->setSpacing(0);
    if (!loose.isEmpty()) {
        auto* area = new QWidget(form);
        area->setObjectName("vclLooseControls");
        area->setProperty("vclClass", "TPanel");
        // the area: around the controls, with the design's margins (on a design canvas larger than
        // the window, as when pages sat side by side, not up to the canvas's edges)
        const int left = lefts.isEmpty() ? 0 : designRect(lefts.last()).right() + 1;
        QRect all;
        for (QWidget* w : loose) all |= designRect(w);
        const int mx = std::max(0, all.left() - left), my = std::max(0, all.top() - top);
        int right = rights.isEmpty() ? all.right() + 1 + mx : designRect(rights.first()).left();
        int below = !clients.isEmpty() ? designRect(clients.first()).top()
                    : (!bottoms.isEmpty() && bottom > all.bottom()) ? bottom : all.bottom() + 1 + my;
        right = std::max(right, all.right() + 1);
        below = std::max(below, all.bottom() + 1);
        QRect r(left, top, right - left, below - top);
        r |= all; // (a control over a bar: kept whole)
        area->setGeometry(r);
        for (QWidget* w : loose) {
            const QRect d = designRect(w);
            const bool shown = !w->isHidden();
            w->setParent(area);
            place(w, QRect(d.topLeft() - r.topLeft(), d.size())); // (its design size, not as shown now)
            w->setVisible(shown);
        }
        for (auto [k, v] : {std::pair{"vclDesignX", r.x()}, {"vclDesignY", r.y()}, {"vclDesignW", r.width()},
                            {"vclDesignH", r.height()}, {"vclDesignCW", r.width()}, {"vclDesignCH", r.height()}})
            area->setProperty(k, v);
        autoLayout(area);
        area->setAutoFillBackground(false);
        centre->addWidget(area, clients.isEmpty() ? 1 : 0);
    }
    for (QWidget* w : clients) {
        if (!single(w)) autoLayout(w);
        w->setMinimumSize(designRect(w).size());
        w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        centre->addWidget(w, 1);
    }
    middle->addLayout(centre, 1);
    for (QWidget* w : rights) {
        autoLayout(w);
        w->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        middle->addWidget(w);
    }
    lay->addLayout(middle, 1);
    for (QWidget* w : bottoms) {
        bar(w, true);
        lay->addWidget(w);
    }
    form->removeEventFilter(filter());
    form->setProperty("vclLaidOut", true);
    // the window as big as its contents needs (not smaller than designed); resizable with a fill area
    form->setMinimumSize(0, 0);
    form->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    form->setLayout(lay);
    // EL_DEBUG_LAYOUT=1: the parts that need more room than designed (to find what makes a window bigger)
    if (qEnvironmentVariableIsSet("EL_DEBUG_LAYOUT")) {
        std::function<void(QWidget*, int)> dump = [&](QWidget* w, int d) {
            const QSize des(w->property("vclDesignW").toInt(), w->property("vclDesignH").toInt());
            const QSize need = (w->layout() ? w->layout()->minimumSize() : w->minimumSize()).expandedTo(w->minimumSize());
            if (need.width() > des.width() + 8 || need.height() > des.height() + 8)
                qWarning().noquote() << QString(d * 2, ' ') + w->objectName() << w->metaObject()->className() << "design" << des
                                     << "needs" << need;
            for (QObject* o : w->children())
                if (auto* k = qobject_cast<QWidget*>(o); k && k->property("vclDesignW").isValid()) dump(k, d + 1);
        };
        qWarning().noquote() << "layoutForm" << form->objectName() << "needs" << lay->minimumSize();
        std::function<void(QLayoutItem*, int)> tree = [&](QLayoutItem* it, int d) {
            if (QWidget* w = it->widget()) {
                qWarning().noquote() << QString(d * 2, ' ') + w->objectName() << w->geometry();
                if (w->layout()) tree(w->layout(), d + 1);
            } else if (QLayout* l = it->layout()) {
                qWarning().noquote() << QString(d * 2, ' ') + typeid(*l).name() << l->geometry();
                for (int i = 0; i < l->count(); ++i) tree(l->itemAt(i), d + 1);
            }
        };
        if (qEnvironmentVariable("EL_DEBUG_LAYOUT") == "2") tree(lay, 0);
        dump(form, 0);
    }
    if (clients.isEmpty()) {
        lay->setSizeConstraint(QLayout::SetFixedSize);
    } else {
        const QSize need = lay->minimumSize();
        form->setMinimumSize(need);
        form->resize(need.expandedTo(QSize(fw, fh)));
    }
}

} // namespace eld::vcl
