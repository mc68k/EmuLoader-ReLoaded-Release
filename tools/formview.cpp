// formview - load a converted .ui form at runtime and save a screenshot.
// Used to check converted forms against the Delphi originals.
//   formview <form.ui> <out.png> [width height] [panelToShow]
#include "ElWidgets.h"
#include "VclRuntime.h"

#include <QApplication>
#include <QFile>
#include <QUiLoader>
#include <QWidget>
#include <cstdio>

class Loader : public QUiLoader {
public:
    QWidget* createWidget(const QString& cls, QWidget* parent, const QString& name) override {
        QWidget* w = nullptr;
        if (cls == "ElPanel") w = new ElPanel(parent);
        else if (cls == "ElToolBar") w = new ElToolBar(parent);
        else if (cls == "ElGroupBox") w = new ElGroupBox(parent);
        else if (cls == "ElLabel") w = new ElLabel(parent);
        else if (cls == "ElButton") w = new ElButton(parent);
        else if (cls == "ElImage") w = new ElImage(parent);
        else if (cls == "ElShape") w = new ElShape(parent);
        else if (cls == "ElSplitter") w = new ElSplitter(parent);
        else if (cls == "ElColorBox") w = new ElColorBox(parent);
        if (w) { w->setObjectName(name); return w; }
        return QUiLoader::createWidget(cls, parent, name);
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc < 3) { std::fprintf(stderr, "usage: formview form.ui out.png [w h] [panel]\n"); return 2; }
    QFile f(argv[1]);
    if (!f.open(QFile::ReadOnly)) return 1;
    Loader loader;
    QWidget* w = loader.load(&f);
    if (!w) { std::fprintf(stderr, "load failed: %s\n", qPrintable(loader.errorString())); return 1; }
    eld::vcl::apply(w);
    if (argc >= 6) { // show one design-time panel at the origin, as the Delphi code does at runtime
        if (auto* p = w->findChild<QWidget*>(argv[5])) {
            p->move(p->x(), p->y());
            for (QObject* o : p->parentWidget()->children())
                if (auto* s = qobject_cast<QWidget*>(o); s && s != p && s->property("vclDesignX").toInt() >= 300 &&
                                                         !s->property("vclAlign").isValid())
                    s->hide();
            p->show();
            p->move(0, p->y());
        }
    }
    if (argc >= 5) w->resize(atoi(argv[3]), atoi(argv[4]));
    w->show();
    app.processEvents();
    w->grab().save(argv[2]);
    return 0;
}
