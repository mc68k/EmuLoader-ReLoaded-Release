#include "ImageLayouts.h"
#include "ElWidgets.h"
#include "ImageView.h"
#include "VclRuntime.h"
#include "WinIni.h"

#include <QCoreApplication>
#include <QWidget>
#include <algorithm>

namespace eld {

namespace {

const char* kSections[ImageLayouts::Count] = {
    QT_TRANSLATE_NOOP("ImageLayout", "Single"), QT_TRANSLATE_NOOP("ImageLayout", "Dual [Layout 1]"), QT_TRANSLATE_NOOP("ImageLayout", "Dual [Layout 2]"), QT_TRANSLATE_NOOP("ImageLayout", "Triple [Layout 1]"), QT_TRANSLATE_NOOP("ImageLayout", "Triple [Layout 2]"), QT_TRANSLATE_NOOP("ImageLayout", "Triple [Layout 3]"),
    QT_TRANSLATE_NOOP("ImageLayout", "Triple [Layout 4]"), QT_TRANSLATE_NOOP("ImageLayout", "Triple [Layout 5]"), QT_TRANSLATE_NOOP("ImageLayout", "Triple [Layout 6]"), QT_TRANSLATE_NOOP("ImageLayout", "Quad [Layout 1 Hor]"), QT_TRANSLATE_NOOP("ImageLayout", "Quad [Layout 1 Vert]"),
    QT_TRANSLATE_NOOP("ImageLayout", "Quad [Layout 2]"), QT_TRANSLATE_NOOP("ImageLayout", "Quad [Layout 3]"), QT_TRANSLATE_NOOP("ImageLayout", "Quad [Layout 4]"), QT_TRANSLATE_NOOP("ImageLayout", "Quad [Layout 5]"), QT_TRANSLATE_NOOP("ImageLayout", "Quad [Layout 6]"), QT_TRANSLATE_NOOP("ImageLayout", "Quad [Layout 7]"),
    QT_TRANSLATE_NOOP("ImageLayout", "Quad [Layout 8]"), QT_TRANSLATE_NOOP("ImageLayout", "Quad [Layout 9]"), QT_TRANSLATE_NOOP("ImageLayout", "Quad [Layout 10]"), QT_TRANSLATE_NOOP("ImageLayout", "Quad [Layout 11]"), QT_TRANSLATE_NOOP("ImageLayout", "Quad [Layout 12]"),
    QT_TRANSLATE_NOOP("ImageLayout", "Quad [Layout 13]"), QT_TRANSLATE_NOOP("ImageLayout", "Quad [Layout 14]"), QT_TRANSLATE_NOOP("ImageLayout", "Quad [Layout 15]"), QT_TRANSLATE_NOOP("ImageLayout", "Quad [Layout 16]"), QT_TRANSLATE_NOOP("ImageLayout", "Quad [Layout 17]")};

const char* kIcons[ImageLayouts::Count] = {
    "img1_layout1", "img2_layout1", "img2_layout2", "img3_layout1", "img3_layout2", "img3_layout3", "img3_layout4",
    "img3_layout5", "img3_layout6", "img4_layout1_hor", "img4_layout1_vert", "img4_layout2", "img4_layout3",
    "img4_layout4", "img4_layout5", "img4_layout6", "img4_layout7", "img4_layout8", "img4_layout9", "img4_layout10",
    "img4_layout11", "img4_layout12", "img4_layout13", "img4_layout14", "img4_layout15", "img4_layout16",
    "img4_layout17"};

// GetScrLayoutDefaultType, images 1..4.
const int kDefaults[ImageLayouts::Count][4] = {
    {1, -1, -1, -1}, {1, 0, -1, -1}, {1, 0, -1, -1}, {1, 0, 3, -1}, {1, 0, 4, -1}, {1, 0, 2, -1}, {1, 0, 2, -1},
    {1, 3, 4, -1}, {1, 3, 4, -1}, {0, 2, 1, 3}, {1, 2, 0, 3}, {1, 0, 3, 2}, {1, 0, 3, 2}, {1, 0, 3, 4}, {1, 0, 3, 4},
    {1, 0, 2, 4}, {1, 0, 2, 4}, {1, 0, 2, 4}, {1, 0, 2, 4}, {1, 0, 3, 2}, {1, 0, 3, 2}, {1, 0, 3, 2}, {1, 0, 3, 2},
    {0, 1, 3, 4}, {1, 0, 2, 3}, {1, 0, 3, 4}, {1, 0, 3, 4}};

// ReadScreenshotLayout / UpdateScreenshotLayout: the ImgSize[] keys of each layout and their
// defaults (the panel's width 'W' or height 'H' divided by div, as the original computes them).
struct SizeKey { const char* key; char basis; double div; };
struct LayoutSizes { SizeKey k[3]; };
const LayoutSizes kSizes[ImageLayouts::Count] = {
    {},
    {{{"height1", 'H', 2}}},
    {{{"width1", 'W', 2}}},
    {{{"height1", 'H', 3}, {"height3", 'H', 3}}},
    {{{"width1", 'W', 3}, {"width3", 'W', 3}}},
    {{{"width1", 'W', 2}, {"height3", 'H', 2}}},
    {{{"width1", 'W', 2}, {"height3", 'H', 2}}},
    {{{"height1", 'H', 2}, {"width3", 'W', 2}}},
    {{{"height1", 'H', 2}, {"width3", 'W', 2}}},
    {{{"width1", 'W', 2}, {"width3", 'W', 2}, {"height3", 'H', 2}}},
    {{{"width1", 'W', 2}, {"height1", 'H', 2}, {"height2", 'H', 2}}},
    {{{"width1", 'W', 3}, {"width3", 'W', 3}, {"height4", 'H', 3}}},
    {{{"width1", 'W', 3}, {"width3", 'W', 3}, {"height4", 'H', 3}}},
    {{{"width1", 'H', 2}, {"height2", 'H', 3}, {"height4", 'W', 3}}},
    {{{"width1", 'H', 2}, {"height2", 'H', 3}, {"height4", 'W', 3}}},
    {{{"width1", 'H', 3}, {"width2", 'H', 3}, {"height4", 'W', 3}}},
    {{{"width1", 'H', 3}, {"width2", 'H', 3}, {"height4", 'W', 3}}},
    {{{"width1", 'H', 3}, {"width2", 'H', 3}, {"height4", 'W', 3}}},
    {{{"width1", 'H', 3}, {"width2", 'H', 3}, {"height4", 'W', 3}}},
    {{{"width1", 'H', 2}, {"height2", 'H', 3}, {"height4", 'W', 3}}},
    {{{"width1", 'H', 2}, {"height2", 'H', 3}, {"height4", 'W', 3}}},
    {{{"width1", 'H', 2}, {"height2", 'H', 3}, {"height4", 'W', 3}}},
    {{{"width1", 'H', 2}, {"height2", 'H', 3}, {"height4", 'W', 3}}},
    {{{"height1", 'H', 2}, {"width3", 'H', 3}, {"width4", 'W', 3}}},
    {{{"width1", 'H', 2}, {"height3", 'H', 3}, {"height4", 'W', 3}}},
    {{{"width1", 'W', 2.5}, {"width3", 'W', 4.5}, {"height3", 'H', 2}}},
    {{{"width1", 'W', 2.5}, {"width3", 'W', 4.5}, {"height3", 'H', 2}}},
};

int defaultSize(const SizeKey& k, int w, int h) { return int((k.basis == 'W' ? w : h) / k.div); }

} // namespace

QString ImageLayouts::section(int l) { return kSections[std::clamp(l, 0, Count - 1)]; }
QString ImageLayouts::title(int l) { return QCoreApplication::translate("ImageLayout", kSections[std::clamp(l, 0, Count - 1)]); }
QString ImageLayouts::iconFile(int l) { return kIcons[std::clamp(l, 0, Count - 1)]; }
int ImageLayouts::imageCount(int l) { return l == 0 ? 1 : l <= 2 ? 2 : l <= 8 ? 3 : 4; }
int ImageLayouts::defaultCategory(int l, int image) {
    return kDefaults[std::clamp(l, 0, Count - 1)][std::clamp(image, 1, 4) - 1];
}

ImageLayouts::Settings ImageLayouts::read(const WinIni& ini, int l, int w, int h) {
    Settings s;
    QString sec = section(l);
    for (int i = 0; i < 3; ++i)
        if (const SizeKey& k = kSizes[l].k[i]; k.key) s.size[i] = ini.readInteger(sec, k.key, defaultSize(k, w, h));
    // FixInvalidImageCategory: an undefined (-1) category is the game snapshot.
    auto fix = [](int c) { return c < 0 ? 1 : c; };
    for (int i = 1; i <= imageCount(l); ++i) {
        s.category[i - 1] = fix(ini.readInteger(sec, QString("image%1_category").arg(i), defaultCategory(l, i)));
        s.consComp[i - 1] = fix(ini.readInteger(sec, QString("image%1conscomp_category").arg(i), defaultCategory(l, i)));
        s.video[i - 1] = ini.readBool(sec, QString("image%1_video").arg(i), false);
        s.videoCC[i - 1] = ini.readBool(sec, QString("image%1conscomp_video").arg(i), false);
        if (s.video[i - 1]) s.category[i - 1] = 1;
        if (s.videoCC[i - 1]) s.consComp[i - 1] = 1;
    }
    if (l > 0 && l < 9) s.enabled[0] = ini.readBool(sec, "image2_enabled", true);  // no visibility for quad layouts
    if (l > 2 && l < 9) s.enabled[1] = ini.readBool(sec, "image3_enabled", true);
    s.visible = ini.readBool(sec, "visible", true); // (in the < > layout buttons)
    return s;
}

void ImageLayouts::writeCategories(WinIni& ini, int l, const Settings& s) {
    QString sec = section(l);
    for (int i = 1; i <= imageCount(l); ++i) {
        ini.writeInteger(sec, QString("image%1_category").arg(i), s.category[i - 1]);
        ini.writeInteger(sec, QString("image%1conscomp_category").arg(i), s.consComp[i - 1]);
        ini.writeInteger(sec, QString("image%1_video").arg(i), s.video[i - 1] ? 1 : 0);
        ini.writeInteger(sec, QString("image%1conscomp_video").arg(i), s.videoCC[i - 1] ? 1 : 0);
    }
    if (l > 0 && l < 9) ini.writeInteger(sec, "image2_enabled", s.enabled[0] ? 1 : 0);
    if (l > 2 && l < 9) ini.writeInteger(sec, "image3_enabled", s.enabled[1] ? 1 : 0);
    ini.writeInteger(sec, "visible", s.visible ? 1 : 0);
}

bool ImageLayouts::useConsComp(const WinIni& ini) {
    return ini.readBool("settings", "UseCustomCategoryConsoleComputer", false);
}

ImageLayouts::ImageLayouts(QWidget* host, ImageView* view1) : host_(host) { views_[0] = view1; }

ImageLayouts::~ImageLayouts() = default; // the widgets belong to the host

void ImageLayouts::clear() {
    // DestroyImageLayoutControls: back to just Images on PanelImage.
    if (views_[0]->parentWidget() != host_) vcl::reparent(views_[0], host_, QPoint(0, 0));
    for (int i = 1; i < 4; ++i) {
        delete views_[i];
        views_[i] = nullptr;
    }
    qDeleteAll(splitters_);
    splitters_.clear();
    for (int i = panels_.size() - 1; i >= 0; --i) delete panels_[i]; // inner panels first
    panels_.clear();
    sized_ = {};
}

// Gives w its Align in parent. VCL aligns the controls of one kind (alTop, alBottom, ...) in
// the order of their positions; here the creation order decides, which is what the original's
// code relies on (each control is created next to the previous one).
void ImageLayouts::add(QWidget* w, QWidget* parent, const char* align, int size) {
    int n = ++order_;
    QString al = align;
    QSize sz = w->size();
    if (size >= 0) {
        if (al == "alTop" || al == "alBottom") sz.setHeight(size);
        else sz.setWidth(size);
    }
    QPoint pos(0, 0);
    if (al == "alTop") pos = QPoint(0, n);
    else if (al == "alBottom") pos = QPoint(0, 100000 - n);
    else if (al == "alLeft") pos = QPoint(n, 0);
    else if (al == "alRight") pos = QPoint(100000 - n, 0);
    w->resize(sz);
    w->setProperty("vclAlign", al);
    vcl::reparent(w, parent, pos);
    w->show();
}

ImageView* ImageLayouts::image(int n, QWidget* parent, const char* align, int sizeIndex) {
    ImageView*& v = views_[n - 1];
    if (!v) v = newView_();
    int size = sizeIndex >= 0 ? settings_.size[sizeIndex] : -1;
    add(v, parent, align, size);
    if (sizeIndex >= 0) {
        sized_[sizeIndex] = v;
        sizedDim_[sizeIndex] = (QString(align) == "alTop" || QString(align) == "alBottom") ? 'h' : 'w';
    }
    return v;
}

void ImageLayouts::splitter(QWidget* parent, const char* align) {
    auto* s = new ElSplitter(parent);
    QString al = align;
    s->resize(7, 7); // SplitterImg[].Width/Height:= 7
    splitters_ << s;
    add(s, parent, align);
}

QWidget* ImageLayouts::panel(QWidget* parent, const char* align, int sizeIndex) {
    auto* p = new QWidget(parent); // ImagePnl / ImagePnlQuadVert (TPanel, bvNone)
    panels_ << p;
    int size = sizeIndex >= 0 ? settings_.size[sizeIndex] : -1;
    add(p, parent, align, size);
    if (sizeIndex >= 0) {
        sized_[sizeIndex] = p;
        sizedDim_[sizeIndex] = (QString(align) == "alTop" || QString(align) == "alBottom") ? 'h' : 'w';
    }
    return p;
}

void ImageLayouts::build(int l, const Settings& s, const std::function<ImageView*()>& newView) {
    clear();
    layout_ = std::clamp(l, 0, Count - 1);
    settings_ = s;
    newView_ = newView;
    order_ = 0;
    QWidget* P = host_;
    bool e2 = s.enabled[0], e3 = s.enabled[1];
    auto main = [&](QWidget* parent, const char* align, int sizeIndex = -1) { image(1, parent, align, sizeIndex); };
    // Dual and triple layouts with their extra images turned off show just image 1.
    bool any = layout_ <= 2 ? e2 : layout_ <= 8 ? (e2 || e3) : true;
    if (layout_ == 0 || !any) {
        main(P, "alClient");
    } else {
        switch (layout_) {
        case 1: main(P, "alTop", 0); splitter(P, "alTop"); image(2, P, "alClient"); break;
        case 2: main(P, "alLeft", 0); splitter(P, "alLeft"); image(2, P, "alClient"); break;
        case 3:
            main(P, "alTop", 0);
            splitter(P, "alTop");
            if (e2 && e3) {
                image(3, P, "alBottom", 1);
                splitter(P, "alBottom");
                image(2, P, "alClient");
            } else image(e2 ? 2 : 3, P, "alClient");
            break;
        case 4:
            main(P, "alLeft", 0);
            splitter(P, "alLeft");
            if (e2 && e3) {
                image(3, P, "alRight", 1);
                splitter(P, "alRight");
                image(2, P, "alClient");
            } else image(e2 ? 2 : 3, P, "alClient");
            break;
        case 5: case 6: {
            const char* al3 = layout_ == 5 ? "alTop" : "alBottom";
            if (e2) {
                main(P, "alLeft", 0);
                splitter(P, "alLeft");
                if (e3) {
                    image(3, P, al3, 1);
                    splitter(P, al3);
                }
                image(2, P, "alClient");
            } else {
                image(3, P, al3, 1);
                splitter(P, al3);
                main(P, "alClient");
            }
            break;
        }
        case 7: case 8: {
            const char* al3 = layout_ == 7 ? "alLeft" : "alRight";
            if (e2 && e3) {
                image(3, P, al3, 1);
                splitter(P, al3);
                QWidget* pnl = panel(P, "alClient");
                main(pnl, "alTop", 0);
                splitter(pnl, "alTop");
                image(2, pnl, "alClient");
            } else if (e2) {
                main(P, "alTop", 0);
                splitter(P, "alTop");
                image(2, P, "alClient");
            } else {
                image(3, P, al3, 1);
                splitter(P, al3);
                main(P, "alClient");
            }
            break;
        }
        case 9: {
            QWidget* pnl = panel(P, "alBottom", 2);
            main(P, "alLeft", 0);
            splitter(P, "alLeft");
            image(2, P, "alClient");
            splitter(P, "alBottom");
            image(3, pnl, "alLeft", 1);
            splitter(pnl, "alLeft");
            image(4, pnl, "alClient");
            break;
        }
        case 10: {
            QWidget* pnl = panel(P, "alLeft", 0);
            main(pnl, "alTop", 1);
            splitter(pnl, "alTop");
            image(3, pnl, "alClient");
            splitter(P, "alLeft");
            QWidget* vert = panel(P, "alClient");
            image(2, vert, "alTop", 2);
            splitter(vert, "alTop");
            image(4, vert, "alClient");
            break;
        }
        case 11: case 12: {
            const char* al4 = layout_ == 11 ? "alTop" : "alBottom";
            image(4, P, al4, 2);
            splitter(P, al4);
            main(P, "alLeft", 0);
            splitter(P, "alLeft");
            image(3, P, "alRight", 1);
            splitter(P, "alRight");
            image(2, P, "alClient");
            break;
        }
        case 13: case 14: {
            const char* al1 = layout_ == 13 ? "alLeft" : "alRight";
            main(P, al1, 0);
            splitter(P, al1);
            QWidget* pnl = panel(P, "alClient");
            image(2, pnl, "alTop", 1);
            splitter(pnl, "alTop");
            image(4, pnl, "alBottom", 2);
            splitter(pnl, "alBottom");
            image(3, pnl, "alClient");
            break;
        }
        case 15: case 16: case 17: case 18: {
            const char* al1 = (layout_ == 15 || layout_ == 16) ? "alLeft" : "alRight";
            const char* al4 = (layout_ == 15 || layout_ == 17) ? "alTop" : "alBottom";
            main(P, al1, 0);
            splitter(P, al1);
            QWidget* pnl = panel(P, "alClient");
            image(4, pnl, al4, 2);
            splitter(pnl, al4);
            image(2, pnl, "alLeft", 1);
            splitter(pnl, "alLeft");
            image(3, pnl, "alClient");
            break;
        }
        case 19: case 20: case 21: case 22: {
            const char* al1 = (layout_ == 19 || layout_ == 21) ? "alLeft" : "alRight";
            const char* al4 = (layout_ == 19 || layout_ == 20) ? "alTop" : "alBottom";
            main(P, al1, 0);
            image(4, P, al4, 2);
            splitter(P, al4);
            splitter(P, al1);
            QWidget* pnl = panel(P, "alClient");
            image(2, pnl, "alTop", 1);
            splitter(pnl, "alTop");
            image(3, pnl, "alClient");
            break;
        }
        case 23: {
            image(3, P, "alLeft", 1);
            splitter(P, "alLeft");
            image(4, P, "alRight", 2);
            splitter(P, "alRight");
            QWidget* pnl = panel(P, "alClient");
            main(pnl, "alTop", 0);
            splitter(pnl, "alTop");
            image(2, pnl, "alClient");
            break;
        }
        case 24:
            main(P, "alLeft", 0);
            splitter(P, "alLeft");
            image(3, P, "alTop", 1);
            splitter(P, "alTop");
            image(4, P, "alBottom", 2);
            splitter(P, "alBottom");
            image(2, P, "alClient");
            break;
        case 25: case 26: {
            const char* al1 = layout_ == 25 ? "alLeft" : "alRight";
            const char* alP = layout_ == 25 ? "alRight" : "alLeft";
            main(P, al1, 0);
            splitter(P, al1);
            QWidget* pnl = panel(P, alP, 1);
            splitter(P, alP);
            image(2, P, "alClient");
            image(3, pnl, "alTop", 2);
            splitter(pnl, "alTop");
            image(4, pnl, "alClient");
            break;
        }
        }
    }
    // Lay out from the outside in.
    vcl::relayout(P);
    for (QWidget* p : panels_) vcl::relayout(p);
}

void ImageLayouts::writeSizes(WinIni& ini) const {
    if (layout_ == 0) return;
    QString sec = section(layout_);
    for (int i = 0; i < 3; ++i) {
        const SizeKey& k = kSizes[layout_].k[i];
        if (!k.key || !sized_[i]) continue; // not shown (an image turned off)
        ini.writeInteger(sec, k.key, sizedDim_[i] == 'w' ? sized_[i]->width() : sized_[i]->height());
    }
}

void ImageLayouts::keepCurrentSizes() {
    for (int i = 0; i < 3; ++i)
        if (kSizes[layout_].k[i].key && sized_[i])
            settings_.size[i] = sizedDim_[i] == 'w' ? sized_[i]->width() : sized_[i]->height();
}

void ImageLayouts::centerSplitter(const QWidget* splitter) {
    if (layout_ == 0 || !splitter) return;
    keepCurrentSizes();
    // the picture (or panel) the splitter sizes: the one beside it, aligned the same way
    for (int i = 0; i < 3; ++i) {
        const SizeKey& k = kSizes[layout_].k[i];
        if (!k.key || !sized_[i] || sized_[i]->parentWidget() != splitter->parentWidget() ||
            sized_[i]->property("vclAlign") != splitter->property("vclAlign"))
            continue;
        settings_.size[i] = defaultSize(k, host_->width(), host_->height());
    }
    build(layout_, settings_, newView_);
}

void ImageLayouts::centerSplitters() {
    if (layout_ == 0) return;
    for (int i = 0; i < 3; ++i) {
        const SizeKey& k = kSizes[layout_].k[i];
        if (k.key) settings_.size[i] = defaultSize(k, host_->width(), host_->height());
    }
    build(layout_, settings_, newView_);
}

} // namespace eld
