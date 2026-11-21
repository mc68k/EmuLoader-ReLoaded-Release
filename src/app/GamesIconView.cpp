#include "GamesIconView.h"

#include "ElDatabase.h"
#include "GamesModel.h"
#include "WinIni.h"

#include <QBuffer>
#include <QScrollBar>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>

namespace eld {

// ---------------------------------------------------------------- settings

void ThumbnailSettings::load(const QString& file) {
    WinIni ini;
    ini.load(file);
    const char* s = "Thumbnails";
    width = ini.readInteger(s, "WidthSize", width);
    height = ini.readInteger(s, "HeightSize", height);
    if (width <= 1) width = 144;
    if (height <= 1) height = 178;
    width = std::clamp(width, 64, 640);
    height = std::clamp(height, 64, 640);
    borderColor = ini.readInteger(s, "BorderColor", borderColor);
    showBorder = ini.readBool(s, "ShowBorder", showBorder);
    showParent = ini.readBool(s, "ShowParent", showParent);
    showTitle = ini.readBool(s, "ShowTitle", showTitle);
    keepAspect = ini.readBool(s, "MaintainAspectRatio", keepAspect);
    showSystemIcon = ini.readBool(s, "SystemIconVisible", showSystemIcon);
    systemIconSize = ini.readInteger(s, "SystemIconSize", systemIconSize);
    showMediaTypeIcon = ini.readBool(s, "MediaTypeIconVisible", showMediaTypeIcon);
    mediaTypeIconSize = ini.readInteger(s, "MediaTypeIconSize", mediaTypeIconSize);
    showGameIcon = ini.readBool(s, "GameIconVisible", showGameIcon);
    showFavoriteIcon = ini.readBool(s, "FavoriteIconVisible", showFavoriteIcon);
    showSpecialIcon = ini.readBool(s, "SpecialIconVisible", showSpecialIcon);
    leftAlignIcons = ini.readBool(s, "LeftAlignIcons", leftAlignIcons);
    iconWhenNoImage = ini.readBool(s, "ShowIconInNoThumbnail", iconWhenNoImage);
}

void ThumbnailSettings::save(const QString& file) const {
    WinIni ini;
    ini.load(file);
    const char* s = "Thumbnails";
    ini.writeInteger(s, "WidthSize", width);
    ini.writeInteger(s, "HeightSize", height);
    ini.writeInteger(s, "ImageListSize", width < 144 || height < 178 ? 68 : 128);
    ini.writeInteger(s, "BorderColor", borderColor);
    ini.writeInteger(s, "ShowBorder", showBorder);
    ini.writeInteger(s, "ShowParent", showParent);
    ini.writeInteger(s, "ShowTitle", showTitle);
    ini.writeInteger(s, "MaintainAspectRatio", keepAspect);
    ini.writeInteger(s, "SystemIconVisible", showSystemIcon);
    ini.writeInteger(s, "SystemIconSize", systemIconSize);
    ini.writeInteger(s, "MediaTypeIconVisible", showMediaTypeIcon);
    ini.writeInteger(s, "MediaTypeIconSize", mediaTypeIconSize);
    ini.writeInteger(s, "GameIconVisible", showGameIcon);
    ini.writeInteger(s, "FavoriteIconVisible", showFavoriteIcon);
    ini.writeInteger(s, "SpecialIconVisible", showSpecialIcon);
    ini.writeInteger(s, "LeftAlignIcons", leftAlignIcons);
    ini.writeInteger(s, "ShowIconInNoThumbnail", iconWhenNoImage);
    ini.save(file);
}

// ---------------------------------------------------------------- drawing

namespace {
struct TileLook { int w, h, icon, detailPt; };
// SetTilesViewCellSize: the cell and its icon, by Tiles View Cell Size
const TileLook kTiles[] = {{550, 148, 128, 16}, {396, 106, 96, 12}, {385, 88, 68, 12}, {260, 58, 48, 9}};
} // namespace

class GamesIconDelegate : public QStyledItemDelegate {
public:
    explicit GamesIconDelegate(GamesIconView* v) : QStyledItemDelegate(v), v_(v) {}

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return v_->cellSize(); }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const int row = v_->filter_->mapToSource(index).row();
        const MameGame& g = v_->model_->game(row);
        const QRect cell = option.rect.adjusted(2, 2, -2, -2);
        const bool selected = option.state & QStyle::State_Selected;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        if (selected) selection(p, cell, g);
        const QModelIndex title = index.siblingAtColumn(GamesModel::Title);
        QFont font = option.font;
        if (const QVariant f = title.data(Qt::FontRole); f.isValid()) font = f.value<QFont>();
        QColor text = option.palette.color(QPalette::Text);
        if (const QVariant c = title.data(Qt::ForegroundRole); c.isValid()) text = c.value<QBrush>().color();
        if (selected) text = LightMode::qc(fontColor(g));
        p->setFont(font);
        p->setPen(text);
        const QString caption = title.data(Qt::DisplayRole).toString();
        const QIcon icon = v_->model_->icon(g);
        switch (v_->mode_) {
        case GamesIconView::SmallIcons: {
            icon.paint(p, QRect(cell.left() + 2, cell.top() + (cell.height() - 16) / 2, 16, 16));
            const QRect t(cell.left() + 22, cell.top(), cell.width() - 24, cell.height());
            p->drawText(t, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(font).elidedText(caption, Qt::ElideRight, t.width()));
            break;
        }
        case GamesIconView::LargeIcons: {
            icon.paint(p, QRect(cell.left() + (cell.width() - 32) / 2, cell.top() + 4, 32, 32));
            const QRect t(cell.left() + 2, cell.top() + 40, cell.width() - 4, cell.height() - 42);
            p->drawText(t, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, twoLines(caption, font, t.width()));
            break;
        }
        case GamesIconView::Tiles: {
            const TileLook& tl = kTiles[std::clamp(v_->tileSize_, 0, 3)];
            const int is = tl.icon;
            icon.paint(p, QRect(cell.left() + 4, cell.top() + (cell.height() - is) / 2, is, is));
            const int x = cell.left() + is + 12, w = cell.right() - x - 4;
            QFont detail = font;
            detail.setBold(false);
            detail.setItalic(false);
            detail.setPointSize(std::min(tl.detailPt, std::max(font.pointSize(), 6)));
            const QFontMetrics fm(font), dm(detail);
            QStringList lines;
            QStringList ym;
            if (!g.year.isEmpty()) ym << g.year;
            if (!g.manufacturer.isEmpty()) ym << g.manufacturer;
            lines << ym.join(", ");
            lines << (g.cloneOf.isEmpty() ? g.name : QString("%1 (%2)").arg(g.name, g.cloneOf));
            const int total = fm.height() + int(lines.size()) * dm.height() + 2;
            int y = cell.top() + std::max(2, (cell.height() - total) / 2);
            p->drawText(QRect(x, y, w, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(caption, Qt::ElideRight, w));
            y += fm.height() + 2;
            p->setFont(detail);
            p->setPen(selected ? text : QColor(0x60, 0x60, 0x60));
            for (const QString& l : lines) {
                p->drawText(QRect(x, y, w, dm.height()), Qt::AlignLeft | Qt::AlignVCenter, dm.elidedText(l, Qt::ElideRight, w));
                y += dm.height();
            }
            break;
        }
        case GamesIconView::Thumbnails:
            thumbnail(p, cell, g, row, caption, font, icon);
            break;
        }
        p->restore();
    }

private:
    GamesIconView* v_;

    int fontColor(const MameGame& g) const {
        const LightMode& lm = v_->lm_;
        const bool active = v_->hasFocus() && v_->window()->isActiveWindow();
        return active ? (g.setStatus == 1 ? lm.selMissFont : lm.selFont) : lm.selInactiveFont;
    }

    // the selection bar's colors, as the details list draws them
    void selection(QPainter* p, const QRect& r, const MameGame& g) const {
        const LightMode& lm = v_->lm_;
        const bool active = v_->hasFocus() && v_->window()->isActiveWindow();
        const bool miss = g.setStatus == 1;
        QColor top = LightMode::qc(active ? (miss ? lm.selMissTop : lm.selTop) : lm.selInactiveTop);
        QColor bottom = LightMode::qc(active ? (miss ? lm.selMissBottom : lm.selBottom) : lm.selInactiveBottom);
        const QColor frame = LightMode::qc(active ? (miss ? lm.selMissFrame : lm.selFrame) : lm.selInactiveFrame);
        const int alpha = lm.selAlphaBlend ? 200 : 255;
        top.setAlpha(alpha);
        bottom.setAlpha(alpha);
        QPainterPath path;
        const QRectF b = QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5);
        if (lm.selRoundCorners) path.addRoundedRect(b, 4, 4);
        else path.addRect(b);
        if (lm.selGradient) {
            QLinearGradient grad(b.topLeft(), b.bottomLeft());
            grad.setColorAt(0, top);
            grad.setColorAt(1, bottom);
            p->fillPath(path, grad);
        } else {
            p->fillPath(path, top);
        }
        p->setPen(frame);
        p->drawPath(path);
    }

    static QString twoLines(const QString& s, const QFont& f, int w) {
        const QFontMetrics fm(f);
        if (fm.horizontalAdvance(s) <= w) return s;
        // break at a space that fits, the rest elided on the second line
        int cut = -1;
        for (int i = 1; i < s.size(); ++i)
            if (s[i] == ' ' && fm.horizontalAdvance(s.left(i)) <= w) cut = i;
        if (cut < 0) return fm.elidedText(s, Qt::ElideRight, w);
        return s.left(cut) + "\n" + fm.elidedText(s.mid(cut + 1), Qt::ElideRight, w);
    }

    // GamesListViewItemThumbnailDraw / ItemThumbnailShowIcons
    void thumbnail(QPainter* p, const QRect& cell, const MameGame& g, int row, const QString& caption, const QFont& font,
                   const QIcon& icon) const {
        const ThumbnailSettings& t = v_->thumbs_;
        const QRect area = GamesIconView::imageArea(t, font).translated(cell.topLeft() - QPoint(2, 2));
        const QImage* img = v_->hideImages_ ? nullptr : v_->thumbnail(row);
        if (img && !img->isNull()) {
            QSize s = img->size();
            s.scale(area.size(), t.keepAspect ? Qt::KeepAspectRatio : Qt::IgnoreAspectRatio);
            const QRect at(area.left() + (area.width() - s.width()) / 2, area.top() + (area.height() - s.height()) / 2,
                           s.width(), s.height());
            p->drawImage(at, *img);
            if (t.showBorder) {
                p->setPen(LightMode::qc(t.borderColor));
                p->drawRect(QRectF(at).adjusted(-0.5, -0.5, 0.5, 0.5));
            }
        } else {
            // no picture: the game's icon, large, in its place
            const int is = std::min(128, std::min(area.width(), area.height()) - 8);
            icon.paint(p, QRect(area.left() + (area.width() - is) / 2, area.top() + (area.height() - is) / 2, is, is));
        }
        const bool havePicture = img && !img->isNull();
        // the game, favorite and Special List icons in a corner of the picture (Show Icons With No
        // Thumbnail: also over the large icon)
        if (havePicture || t.iconWhenNoImage) {
            QList<QIcon> marks;
            if (t.showGameIcon && havePicture) marks << icon;
            if (t.showFavoriteIcon && v_->model_->favorites().contains(g.key()) && !v_->favIcon_.isNull()) marks << v_->favIcon_;
            if (t.showSpecialIcon && v_->model_->special().contains(g.key()) && !v_->specialIcon_.isNull()) marks << v_->specialIcon_;
            const int ms = t.mediaTypeIconSize == 1 ? 32 : 24; // Medium (24x24), Large (32x32)
            int x = t.leftAlignIcons ? area.left() + 2 : area.right() - ms - 2;
            for (const QIcon& m : marks) {
                m.paint(p, QRect(x, area.top() + 2, ms, ms));
                x += t.leftAlignIcons ? ms + 2 : -(ms + 2);
            }
        }
        if (t.showTitle) {
            const QRect tr(cell.left() + 2, area.bottom() + 4, cell.width() - 4, cell.bottom() - area.bottom() - 4);
            p->drawText(tr, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, twoLines(caption, font, tr.width()));
        }
    }
};

// ---------------------------------------------------------------- the view

GamesIconView::GamesIconView(GamesModel* model, GamesFilter* filter, QWidget* parent)
    : QListView(parent), model_(model), filter_(filter) {
    setModel(filter);
    setItemDelegate(new GamesIconDelegate(this));
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setLayoutMode(QListView::Batched);
    setBatchSize(500);
    setSpacing(0);
    setMode(LargeIcons);
}

QSize GamesIconView::cellSize() const {
    switch (mode_) {
    case SmallIcons: return QSize(220, 22);
    case LargeIcons: return QSize(104, 78);
    case Tiles: { const TileLook& t = kTiles[std::clamp(tileSize_, 0, 3)]; return QSize(t.w, t.h); }
    case Thumbnails: return QSize(thumbs_.width, thumbs_.height);
    }
    return QSize(104, 78);
}

QRect GamesIconView::imageArea(const ThumbnailSettings& s, const QFont& f) {
    const int captionH = s.showTitle ? QFontMetrics(f).height() * 2 + 4 : 0;
    return QRect(6, 6, s.width - 12, s.height - 12 - captionH);
}

void GamesIconView::setMode(int mode) {
    mode_ = mode;
    setGridSize(cellSize());
    verticalScrollBar()->setSingleStep(std::max(20, cellSize().height() / 3));
    doItemsLayout();
    viewport()->update();
}

void GamesIconView::setTileSize(int tag) {
    tileSize_ = std::clamp(tag, 0, 3);
    if (mode_ == Tiles) setMode(Tiles);
}

void GamesIconView::setThumbnails(const ThumbnailSettings& s) {
    const bool parentChanged = s.showParent != thumbs_.showParent;
    thumbs_ = s;
    if (parentChanged) reloadThumbnails();
    if (mode_ == Thumbnails) setMode(Thumbnails);
}

void GamesIconView::setLook(const LightMode& lm) {
    lm_ = lm;
    viewport()->update();
}

void GamesIconView::reloadThumbnails(bool selectedOnly) {
    if (selectedOnly) {
        for (const QModelIndex& i : selectionModel()->selectedIndexes()) {
            const QString n = model_->game(filter_->mapToSource(i).row()).key();
            cache_.remove(n);
            none_.remove(n);
        }
    } else {
        cache_.clear();
        none_.clear();
        pending_.clear();
        ++generation_; // what's still being read is dropped
    }
    viewport()->update();
}

namespace {
class ThumbJob : public QRunnable {
public:
    ThumbJob(QPointer<GamesIconView> v, QString name, ImageFinder::Found f, QSize size, int gen,
             std::shared_ptr<std::atomic<int>> scrolled, std::function<void(GamesIconView*, const QString&, const QImage&, int, bool)> done)
        : v_(v), name_(std::move(name)), f_(std::move(f)), size_(size), gen_(gen), scrolled_(std::move(scrolled)),
          scrolledWhenAsked_(scrolled_->load()), done_(std::move(done)) {}
    void run() override {
        QImage img;
        bool dropped = false;
        if (f_.ok) {
            auto stillWanted = [this] { return scrolled_->load() == scrolledWhenAsked_; };
            QByteArray data = ImageFinder::read(f_, stillWanted);
            dropped = data.isEmpty() && !stillWanted();
            QBuffer buf(&data);
            QImageReader r(&buf);
            r.setAutoTransform(true);
            QSize s = r.size();
            if (s.isValid()) {
                s.scale(size_, Qt::KeepAspectRatio);
                if (s.width() < r.size().width()) r.setScaledSize(s);
            }
            img = r.read();
        }
        QPointer<GamesIconView> v = v_;
        const QString name = name_;
        const int gen = gen_;
        auto done = done_;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [v, name, img, gen, done, dropped] {
            if (v) done(v.data(), name, img, gen, dropped);
        }, Qt::QueuedConnection);
    }

private:
    QPointer<GamesIconView> v_;
    QString name_;
    ImageFinder::Found f_;
    QSize size_;
    int gen_;
    std::shared_ptr<std::atomic<int>> scrolled_;
    int scrolledWhenAsked_;
    std::function<void(GamesIconView*, const QString&, const QImage&, int, bool)> done_;
};
} // namespace

void GamesIconView::scrollContentsBy(int dx, int dy) {
    ++*scrolled_;
    QListView::scrollContentsBy(dx, dy);
}

const QImage* GamesIconView::thumbnail(int sourceRow) {
    const MameGame& g = model_->game(sourceRow);
    const QString key = g.key();
    if (const QImage* img = cache_.object(key)) return img;
    if (none_.contains(key) || pending_.contains(key) || !findSnapshot) return nullptr;
    const ImageFinder::Found f = findSnapshot(g.imageName(), thumbs_.showParent ? g.imageParent() : QString());
    if (!f.ok) {
        none_.insert(key);
        return nullptr;
    }
    pending_.insert(key);
    // read at the size it's drawn (twice that on a high resolution screen)
    const QSize size = QSize(thumbs_.width, thumbs_.height) * std::max<qreal>(1.0, devicePixelRatioF());
    QThreadPool::globalInstance()->start(new ThumbJob(this, key, f, size, generation_, scrolled_,
        [](GamesIconView* v, const QString& name, const QImage& img, int gen, bool dropped) {
            if (gen != v->generation_) return;
            if (dropped) { // (scrolled past before it was read: asked again if it's still in sight)
                v->pending_.remove(name);
                v->viewport()->update();
                return;
            }
            v->thumbnailReady(name, img);
        }));
    return nullptr;
}

void GamesIconView::thumbnailReady(const QString& name, const QImage& img) {
    pending_.remove(name);
    if (img.isNull()) none_.insert(name);
    else cache_.insert(name, new QImage(img), 1);
    viewport()->update();
}

} // namespace eld
