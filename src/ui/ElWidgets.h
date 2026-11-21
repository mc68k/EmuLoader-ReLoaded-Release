// Qt counterparts of EmuLoader's custom VCL controls.
//
// The converted .ui forms (tools/dfm2ui.py) instantiate these classes. Their
// look is driven by "vcl*" dynamic properties copied from the original .dfm,
// and the drawing follows the original Delphi components (PanelEx.pas,
// ShadowLabel.pas, ...), so a converted window looks like the Delphi one.
#pragma once

#include <QComboBox>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>

namespace eld {

// TPanel / TPanelEx: solid color, bevels, or one of TPanelEx's gradient styles.
class ElPanel : public QFrame {
    Q_OBJECT
public:
    explicit ElPanel(QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent* e) override;
private:
    QImage cache_;
    QSize cacheSize_;
    QString cacheKey_;
};

// TToolBar: a panel that lays out its tool buttons left to right.
class ElToolBar : public ElPanel {
    Q_OBJECT
public:
    explicit ElToolBar(QWidget* parent = nullptr) : ElPanel(parent) {}
};

class ElGroupBox : public QGroupBox {
    Q_OBJECT
public:
    explicit ElGroupBox(QWidget* parent = nullptr) : QGroupBox(parent) {}
};

// TShadowLabel / TLabel: text with an optional 1px drop shadow.
class ElLabel : public QLabel {
    Q_OBJECT
public:
    explicit ElLabel(QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
};

// TBitBtnEx / TSpeedButtonEx / TToolButton. Native look by default (as in the
// light theme); speed buttons with a GroupIndex behave like radio buttons.
class ElButton : public QPushButton {
    Q_OBJECT
public:
    explicit ElButton(QWidget* parent = nullptr);
    // Tool bar style: the icon at the top and this caption under it, centered, on up to two
    // lines (property "elCaption"; the text of the button stays empty, its tooltip explains).
    void setCaption(const QString& caption);
    QString caption() const { return caption_; }
private:
    QString caption_;
protected:
    void paintEvent(QPaintEvent*) override;
};

class ElImage : public QLabel {
    Q_OBJECT
public:
    explicit ElImage(QWidget* parent = nullptr);
};

// TShape / TBevel
class ElShape : public QFrame {
    Q_OBJECT
public:
    explicit ElShape(QWidget* parent = nullptr) : QFrame(parent) {}
protected:
    void paintEvent(QPaintEvent*) override;
};

// TSplitterEx: drags the neighboring aligned control to resize it.
class ElSplitter : public QFrame {
    Q_OBJECT
public:
    explicit ElSplitter(QWidget* parent = nullptr);
    // TTMSStyle index (tsSilver, tsSolidColor, tsOffice2003Blue, ... tsOffice2010Black) and the
    // tsSolidColor colors; grip = sgDots / sgNone.
    void setTmsStyle(int style, const QColor& single, const QColor& singleHot);
    void setGrip(bool dots) { grip_ = dots; update(); }
signals:
    void moved();
protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override { hot_ = true; update(); }
    void leaveEvent(QEvent*) override { hot_ = false; update(); }
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    QWidget* target() const;
    int style_ = -1;           // -1: from vclStyle
    QColor single_ = QColor(0xf0f0f0), singleHot_ = Qt::gray;
    bool grip_ = true, hot_ = false;
    QPoint pressPos_;
    int startSize_ = 0;
    QWidget* target_ = nullptr;
};

// TColorBoxEx: a combo listing colors (cbCustomColor, cbStandardColors, cbExtendedColors,
// cbSystemColors, cbPrettyNames), with a swatch per item. Colors are Delphi TColor values
// ($00BBGGRR, or $FF0000nn for a Windows system color) so they're stored as the original did.
class ElColorBox : public QComboBox {
    Q_OBJECT
public:
    explicit ElColorBox(QWidget* parent = nullptr);
    int tcolor() const;                 // the selected color as a Delphi TColor
    void setTColor(int c);              // selects it (adds a custom entry when not listed)
    QColor color() const { return toQColor(tcolor()); }
    int defaultTColor() const;          // DefaultColorColor from the form (vclDefaultColorColor)
    void setToDefault() { setTColor(defaultTColor()); }
    static QColor toQColor(int tcolor);
    static int fromQColor(const QColor& c);
signals:
    void colorChanged();
private:
    void fill();
    int last_ = 0;
};

} // namespace eld

// uic emits class names without a namespace.
using eld::ElButton;
using eld::ElColorBox;
using eld::ElGroupBox;
using eld::ElImage;
using eld::ElLabel;
using eld::ElPanel;
using eld::ElShape;
using eld::ElSplitter;
using eld::ElToolBar;
