// langcheck - development tool: checks a language file against the windows' fixed layouts.
//
//   langcheck <lang.ini> [--grab <folder>]
//
// --grab also saves a picture of every window with the translation, to look over.
//
// Builds the windows that still have fixed places (the main window, its picture selectors, and
// MAME Settings as it's laid out) twice, in English and with the language file, and lists
// the texts that would be cut off: a label, check box, button, group box title or list entry
// whose translation is wider than the room it has (drop-down list entries aren't: a
// list opens as wide as its longest entry) (the control's width, or for an AutoSize label
// the free space up to the next control on the side it grows to), where the English text
// fits - or is wider than an English text that already doesn't fit. Fonts missing on this system
// are replaced, but in both runs, so the comparison holds. Exit code: the number of problems.
#include "Language.h"
#include "ScriptFonts.h"
#include "VclRuntime.h"

#include "ui_FormImageCategorySelector.h"
#include "ui_FormImageLayoutSelector.h"
#include "ui_FormMAMESettings.h"
#include "ui_FormMain.h"

#include <QAbstractButton>
#include <QApplication>
#include <QLayout>
#include <QLoggingCategory>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QGroupBox>
#include <QHash>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QStyle>
#include <QTextStream>
#include <functional>
#include <memory>

namespace {

struct Measure {
    QString text;
    int need = 0;   // pixels the text needs
    int room = 0;   // pixels it has
};

int textWidth(const QFontMetrics& fm, const QString& text) {
    int w = 0;
    for (const QString& line : QString(text).remove('&').split('\n')) w = std::max(w, fm.horizontalAdvance(line));
    return w;
}

// Free space for a control's text: its own width, or up to the next sibling on the side its
// text grows to (Delphi labels are usually AutoSize, so their width is just the English text).
int freeRoom(QWidget* w, bool growRight, bool growLeft) {
    QWidget* p = w->parentWidget();
    if (!p) return w->width();
    QRect g = w->geometry();
    int right = g.right(), left = g.left();
    if (growRight) {
        right = p->contentsRect().right();
        for (QObject* o : p->children()) {
            auto* s = qobject_cast<QWidget*>(o);
            if (!s || s == w || s->isWindow()) continue;
            QRect r = s->geometry();
            if (r.left() > g.left() + 2 && r.top() < g.bottom() && r.bottom() > g.top()) right = std::min(right, r.left() - 2);
        }
    }
    if (growLeft) {
        left = p->contentsRect().left();
        for (QObject* o : p->children()) {
            auto* s = qobject_cast<QWidget*>(o);
            if (!s || s == w || s->isWindow()) continue;
            QRect r = s->geometry();
            if (r.right() < g.right() - 2 && r.top() < g.bottom() && r.bottom() > g.top()) left = std::max(left, r.right() + 2);
        }
    }
    return std::max(g.width(), right - left + 1);
}

// Every text of a window: object name (+ item number) -> what it needs and has.
QHash<QString, Measure> measure(QWidget* form) {
    QHash<QString, Measure> out;
    for (QWidget* w : form->findChildren<QWidget*>()) {
        QString name = w->objectName();
        if (name.isEmpty()) continue;
        QFontMetrics fm(w->font());
        if (auto* l = qobject_cast<QLabel*>(w)) {
            if (l->text().isEmpty() || !l->pixmap().isNull() || l->textFormat() == Qt::RichText || l->wordWrap()) continue;
            Qt::Alignment a = l->alignment();
            bool right = a & Qt::AlignRight, center = a & Qt::AlignHCenter;
            out[name] = {l->text(), textWidth(fm, l->text()) + 2, freeRoom(l, !right && !center, right)};
        } else if (auto* c = qobject_cast<QCheckBox*>(w); c || qobject_cast<QRadioButton*>(w)) {
            auto* b = static_cast<QAbstractButton*>(w);
            if (b->text().isEmpty()) continue;
            int ind = b->style()->pixelMetric(c ? QStyle::PM_IndicatorWidth : QStyle::PM_ExclusiveIndicatorWidth) + 6;
            out[name] = {b->text(), textWidth(fm, b->text()) + ind, b->width()}; // already widened into free space by vcl::apply
        } else if (auto* b = qobject_cast<QPushButton*>(w)) {
            if (b->text().isEmpty()) continue;
            // The main window's tool bar buttons show icons; their text becomes the tooltip.
            QString bar = b->parentWidget() ? b->parentWidget()->objectName() : QString();
            if (bar == "ToolBarButtons" || bar == "ImagesToolBarButtons") continue;
            int icon = b->icon().isNull() ? 0 : b->iconSize().width() + 4;
            out[name] = {b->text(), textWidth(fm, b->text()) + icon + 8, b->width()};
        } else if (auto* g = qobject_cast<QGroupBox*>(w)) {
            if (g->title().isEmpty()) continue;
            out[name] = {g->title(), textWidth(fm, g->title()) + (g->isCheckable() ? 24 : 8), g->width()};
        }
        // (drop-down list entries aren't checked: a list opens as wide as its longest entry)
    }
    return out;
}

QString grabFolder;

template <class Ui>
QHash<QString, Measure> build(const char* name) {
    QWidget w;
    Ui ui;
    ui.setupUi(&w);
    eld::vcl::apply(&w);
    // windows whose pages EmuLoader lays out from their design (vcl::autoLayout): measured so
    if (QStringList{"FormMAMESettings"}.contains(name))
        for (QObject* o : w.children())
            if (auto* page = qobject_cast<QWidget*>(o); page && page->property("vclClass").toString().startsWith("TPanel") &&
                                                        page->property("vclAlign").toString() != "alTop") {
                eld::vcl::autoLayout(page);
                if (page->layout()) {
                    page->layout()->activate();
                    page->resize(page->layout()->minimumSize().expandedTo(page->size()));
                    page->layout()->setGeometry(page->rect());
                }
            }
    w.resize(w.size());
    auto out = measure(&w);
    if (!grabFolder.isEmpty() && QCoreApplication::instance()->property("translated").toBool()) {
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.show();
        QCoreApplication::processEvents();
        w.grab().save(QDir(grabFolder).filePath(QString(name) + ".png"));
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
#ifdef Q_OS_WIN
    // With the real Windows fonts and style, as EmuLoader looks; no window shows on screen.
    QApplication app(argc, argv);
    // The original's .ico files hold PNG pictures with extra bytes after the image; Qt reads them
    // fine, but libpng warns about each one ("IDAT: Extra compressed data").
    QLoggingCategory::setFilterRules("qt.gui.imageio=false");
    app.setStyle("windowsvista");
#else
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    // The original's .ico files hold PNG pictures with extra bytes after the image; Qt reads them
    // fine, but libpng warns about each one ("IDAT: Extra compressed data").
    QLoggingCategory::setFilterRules("qt.gui.imageio=false");
    app.setStyle("Fusion");
#endif
    if (argc < 2) {
        QTextStream(stderr) << "usage: langcheck <lang.ini>\n";
        return 255;
    }
    // The windows with fixed places (the main window's, and MAME Settings' pages as they're laid out
    // here). The windows in layouts and the Settings tabs give their texts the room they need.
    struct Form { const char* name; std::function<QHash<QString, Measure>()> make; };
    const Form forms[] = {
        {"FormMain", [] { return build<Ui::FormMain>("FormMain"); }},
        {"FormMAMESettings", [] { return build<Ui::FormMAMESettings>("FormMAMESettings"); }},
        {"FormImageLayoutSelector", [] { return build<Ui::FormImageLayoutSelector>("FormImageLayoutSelector"); }},
        {"FormImageCategorySelector", [] { return build<Ui::FormImageCategorySelector>("FormImageCategorySelector"); }},
    };
    for (int i = 2; i + 1 < argc; ++i)
        if (QString(argv[i]) == "--grab") grabFolder = QString::fromLocal8Bit(argv[i + 1]);
    if (!grabFolder.isEmpty()) QDir().mkpath(grabFolder);
    QHash<QString, QHash<QString, Measure>> english;
    for (const Form& f : forms) english[f.name] = f.make();
    auto* lf = new eld::LanguageFile;
    if (!lf->load(QString::fromLocal8Bit(argv[1]))) {
        QTextStream(stderr) << "cannot read " << argv[1] << "\n";
        return 255;
    }
    QCoreApplication::installTranslator(lf);
    app.setProperty("translated", true);
    // the language's fonts (Font=, FontSize=), as EmuLoader uses them
    const eld::LanguageFile::Info info = eld::LanguageFile::readInfo(QString::fromLocal8Bit(argv[1]));
    eld::ScriptFonts::setup(info.locale.isEmpty() ? info.id : info.locale, info.font, info.fontSize);
    if (!eld::ScriptFonts::uiFamily().isEmpty() || eld::ScriptFonts::sizeDelta() != 0) {
        QFont af = QApplication::font();
        if (!eld::ScriptFonts::uiFamily().isEmpty()) af.setFamily(eld::ScriptFonts::uiFamily());
        QApplication::setFont(eld::ScriptFonts::shown(af));
    }
    // a larger size: every text is checked, the untranslated ones too
    const bool sized = eld::ScriptFonts::sizeDelta() != 0 || !eld::ScriptFonts::uiFamily().isEmpty();
    QTextStream out(stdout);
    int problems = 0, checked = 0;
    for (const Form& f : forms) {
        auto tr = f.make();
        for (auto it = tr.begin(); it != tr.end(); ++it) {
            const Measure& t = it.value();
            const Measure en = english[f.name].value(it.key());
            if (en.text.isEmpty() || (t.text == en.text && !sized)) continue;
            ++checked;
            bool englishFits = en.need <= en.room;
            if (t.need <= t.room || (!englishFits && t.need <= en.need)) continue;
            ++problems;
            out << f.name << " " << it.key() << ": needs " << t.need << " px, has " << t.room
                << (englishFits ? "" : QString(" (English needs %1)").arg(en.need)) << "\n    \""
                << QString(t.text).replace('\n', "\\n") << "\"  <-  \"" << QString(en.text).replace('\n', "\\n") << "\"\n";
        }
    }
    out << checked << (sized ? " texts checked (with the language's font size), " : " translated texts checked, ")
        << problems << " too long\n";
    return std::min(problems, 254);
}
