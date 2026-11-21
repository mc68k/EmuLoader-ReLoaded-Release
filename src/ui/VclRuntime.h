// VclRuntime - makes a converted form behave like its Delphi original.
//
// Call eld::vcl::apply(form) right after ui.setupUi(form). It reads the
// "vcl*" dynamic properties written by tools/dfm2ui.py and:
//   * applies colors, fonts (pixel sizes) and initial visibility,
//   * reproduces VCL Align (alTop/alBottom/alLeft/alRight/alClient) and
//     Anchors when containers are resized,
//   * makes grouped speed buttons exclusive (GroupIndex).
#pragma once

#include <QColor>
#include <QString>
#include <QVariant>

class QBoxLayout;
class QPushButton;
class QWidget;

namespace eld::vcl {

void apply(QWidget* form);

// Re-run the align/anchor layout of one container (e.g. after showing or
// hiding an aligned child, as VCL does automatically).
void relayout(QWidget* container);

// Move/resize a control from code, like assigning Left/Top/Width/Height (or Parent)
// in Delphi: the new position becomes the reference for its anchors.
void place(QWidget* w, const QRect& r);
void reparent(QWidget* w, QWidget* newParent, const QPoint& pos);

// A converted form's container made flexible: its controls (and those of the panels inside it)
// put in rows and columns from their design positions - side by side what was side by side, one
// under the other what was stacked, with the design's gaps, and every control at least its design
// size. In English it looks as designed; a longer text or a larger font makes room for itself
// (the container grows instead of cutting the text short). Controls that overlap keep their
// design places. Anchored to the right (akRight without akLeft): kept at the right edge.
// After this, relayout() leaves the container to its layout.
void autoLayout(QWidget* container);
bool laidOut(const QWidget* container);
// A whole converted window made flexible: its docked bars (alTop, alBottom, alLeft, alRight) and
// its fill area (alClient) stay docked, each laid out by autoLayout; the controls that aren't
// docked are laid out in the middle. Buttons at the right end of a bottom bar stay at the right.
// The window is as big as its contents needs: resizable when it has a fill area (it takes the
// extra room), else of that size. Call it last in the window's constructor, when the controls
// are where the window's code put them.
void layoutForm(QWidget* form);
// For autoLayout, set before it: a panel lying over its parent's controls (a cover, shown in
// place of them), kept over them with the design's margins, not laid out with them
void setOverlay(QWidget* w);
// For autoLayout: a control kept at the bottom of its row (tabs standing on a line), not centered
void setRowBottom(QWidget* w);
// For autoLayout: a label showing data (a path, a file name) rather than text to translate: it
// keeps its design width, a long one shortened with "…" (and whole in its tooltip), not
// widening the window
void setDataLabel(QWidget* w);

// Show or hide a control and re-align its parent, like setting .Visible in Delphi.
void setVisible(QWidget* w, bool visible);

// Cut a label's or button's text with "…" when it doesn't fit, showing the whole text as the
// tooltip (apply() does this for every form control; for controls made in code).
void elideLongTexts(QWidget* w);
// The other way round: the whole text again and no more cutting short (a control moved into a
// layout, which gives it the room its text needs).
void stopEliding(QWidget* w);

// For windows laid out by hand with Qt's layouts (the form's controls moved into them):
// A control in a layout: its whole text, and no size limits left from the design.
void loose(QWidget* w);
// A button in a layout: the design's width at least, wider for a longer text (by the text, not the
// style's usual minimum width: small buttons like Up and ? stay small).
QPushButton* textButton(QPushButton* b);
// l, with these margins and spacing
QBoxLayout* boxed(QBoxLayout* l, int left, int top, int right, int bottom, int spacing);
// A control made in code in place of one of the form's (a list made instead of the form's
// stand-in): it takes the form control's parent, place and design (so layoutForm lays it out as
// that control), and the form's is deleted. Returns made.
QWidget* replaceDesigned(QWidget* designed, QWidget* made);

QColor color(const QWidget* w, const char* prop, const QColor& fallback = {});

// Night Mode: the forms' own colors (vclColor, vclFontColor, shadows...) are drawn dark: their
// lightness turned around (white -> near black, black and navy -> near white and light blue),
// the hue kept; panels with a gradient take the night panels' colors. recolor() applies it to
// forms already shown.
struct NightLook {
    bool on = false;
    QColor panelTop, panelBottom, panelFrame;
};
void setNight(const NightLook& n);
const NightLook& night();
QColor nightColor(const QColor& c);   // a light-mode color as Night Mode draws it
void recolor(QWidget* form);

// TGaugeBar2 holds floating point values; the converted QSlider stores them x1000.
double sliderValue(const QWidget* slider);
void setSliderValue(QWidget* slider, double v);

// Delphi Format() for a single argument: %d, %s, %1.2f, %3.0f ...
QString delphiFormat(const QString& fmt, double value);
QString str(const QWidget* w, const char* prop);

} // namespace eld::vcl
