#include "GamesListDelegate.h"
#include "GamesModel.h"

#include <QApplication>
#include <QHeaderView>
#include <QPainter>
#include <QPainterPath>
#include <QTreeView>

namespace eld {

GamesListDelegate::GamesListDelegate(QTreeView* view, GamesModel* model, GamesFilter* filter)
    : QStyledItemDelegate(view), view_(view), model_(model), filter_(filter) {}

int GamesListDelegate::favoriteSpace(const QModelIndex& index) const {
    if (index.column() != GamesModel::Title || !model_->options().favoriteIcon || filter_->favoritesOnly()) return 0;
    return 18;
}

void GamesListDelegate::paintFavorite(QPainter* p, const QRect& cell, const QModelIndex& index) const {
    const MameGame& g = model_->game(filter_->mapToSource(index).row());
    if (!model_->favorites().contains(g.key()) || favIcon_.isNull()) return;
    QRect r(cell.left() + 2, cell.top() + (cell.height() - 14) / 2, 14, 14);
    favIcon_.paint(p, r);
}

int GamesListDelegate::band(const QModelIndex& index) const {
    return filter_->grouped() && filter_->groupStart(index.row()) ? GroupBand : 0;
}

QSize GamesListDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    QSize s = QStyledItemDelegate::sizeHint(option, index);
    return QSize(s.width(), s.height() + band(index));
}

// The group's title band: the parent's icon and title, its year and manufacturer, and a line to
// the right edge, across the row.
void GamesListDelegate::paintGroup(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    QHeaderView* h = view_->header();
    const QRect cell(option.rect.left(), option.rect.top(), option.rect.width(), GroupBand);
    // drawn whole by the first column (Title, always shown); the others leave the band alone
    if (h->visualIndex(index.column()) != 0) return;
    const MameGame& g = model_->game(filter_->mapToSource(index).row());
    const int pr = model_->rowOf(GamesModel::groupKey(g));
    const MameGame& parent = pr >= 0 ? model_->game(pr) : g;
    const QRect row(-h->offset() + 4, cell.top(), h->length(), GroupBand);
    p->save();
    p->setClipRect(QRect(0, cell.top(), view_->viewport()->width(), GroupBand));
    const QIcon icon = model_->icon(parent);
    const int iy = row.top() + (GroupBand - 16) / 2 + 2;
    icon.paint(p, QRect(row.left(), iy, 16, 16));
    QFont f = option.font;
    f.setBold(true);
    p->setFont(f);
    p->setPen(groupText_);
    const QRect text(row.left() + 22, row.top() + 2, row.width() - 26, GroupBand - 2);
    const QString title = model_->title(parent);
    p->drawText(text, Qt::AlignLeft | Qt::AlignVCenter, title);
    int x = text.left() + QFontMetrics(f).horizontalAdvance(title) + 8;
    QStringList more;
    if (!parent.year.isEmpty()) more << parent.year;
    if (!parent.manufacturer.isEmpty()) more << parent.manufacturer;
    if (!more.isEmpty()) {
        QFont n = option.font;
        p->setFont(n);
        p->setPen(QColor(Qt::gray));
        const QString m = more.join(", ");
        p->drawText(QRect(x, text.top(), text.right() - x, text.height()), Qt::AlignLeft | Qt::AlignVCenter, m);
        x += QFontMetrics(n).horizontalAdvance(m) + 8;
    }
    const int ly = row.top() + 2 + (GroupBand - 2) / 2 + 1;
    p->setPen(groupLine_);
    if (x < view_->viewport()->width() - 8) p->drawLine(x, ly, view_->viewport()->width() - 8, ly);
    p->restore();
}

bool GamesListDelegate::twoLineCaption(QStyleOptionViewItem* o, int minIconSize) {
    if (o->text.isEmpty() || o->decorationSize.height() < minIconSize) return false;
    const QFontMetrics fm(o->font);
    if (o->rect.height() < 2 * fm.lineSpacing() + 2) return false;
    int w = o->rect.width() - 8;
    if (o->features & QStyleOptionViewItem::HasDecoration) w -= o->decorationSize.width() + 6;
    if (w <= 0 || fm.horizontalAdvance(o->text) <= w) return false;
    // break at the last space that fits; the rest elided on the second line
    int cut = -1;
    for (int i = 1; i < o->text.size(); ++i)
        if (o->text[i] == ' ' && fm.horizontalAdvance(o->text.left(i)) <= w) cut = i;
    if (cut < 0) return false;
    o->text = o->text.left(cut) + QChar::LineSeparator + fm.elidedText(o->text.mid(cut + 1), Qt::ElideRight, w);
    o->features |= QStyleOptionViewItem::WrapText;
    return true;
}

// GameMultilineCaptions: Large (32x32) icons and up, as the original (PopupGameIconSize 0-2)
void GamesListDelegate::initStyleOption(QStyleOptionViewItem* o, const QModelIndex& index) const {
    QStyledItemDelegate::initStyleOption(o, index);
    if (multiline_ && index.column() == GamesModel::Title) twoLineCaption(o, 32);
}

void GamesListDelegate::paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const int b = band(index);
    if (!b) { paintCell(p, option, index); return; }
    paintGroup(p, option, index);
    QStyleOptionViewItem below(option);
    below.rect.setTop(below.rect.top() + b);
    paintCell(p, below, index);
}

void GamesListDelegate::paintCell(QPainter* p, const QStyleOptionViewItem& optionIn, const QModelIndex& index) const {
    QStyleOptionViewItem option(optionIn);
    if (filter_->grouped() && cloneIndent_ && index.column() == GamesModel::Title &&
        !model_->game(filter_->mapToSource(index).row()).cloneOf.isEmpty()) {
        // clones indented by an icon's width (ReportViewFirstColumnIndent); the selection bar
        // still covers the whole row
        const int indent = view_->iconSize().width() + 4;
        if (option.state & QStyle::State_Selected) {
            QStyleOptionViewItem bar(option);
            bar.rect.setWidth(indent);
            paintSelectionBar(p, bar, index);
        } else {
            p->fillRect(QRect(option.rect.left(), option.rect.top(), indent, option.rect.height()), view_->palette().base());
        }
        option.rect.setLeft(option.rect.left() + indent);
    }
    QStyleOptionViewItem o(option);
    initStyleOption(&o, index);
    const int fav = favoriteSpace(index);
    if (!(o.state & QStyle::State_Selected)) {
        if (!fav) {
            QStyledItemDelegate::paint(p, option, index);
            return;
        }
        paintFavorite(p, option.rect, index);
        QStyleOptionViewItem rest(option);
        rest.rect.setLeft(rest.rect.left() + fav);
        QStyledItemDelegate::paint(p, rest, index);
        return;
    }
    const MameGame& g = model_->game(filter_->mapToSource(index).row());
    const bool active = view_->hasFocus() && view_->window()->isActiveWindow();
    const bool miss = g.setStatus == 1; // "Games With Missing ROMs/CHDs"
    const int font = active ? (miss ? lm_.selMissFont : lm_.selFont) : lm_.selInactiveFont;

    paintSelectionBar(p, o, index);

    // The cell's icon and text on top, in the selection font color.
    if (fav) {
        paintFavorite(p, o.rect, index);
        o.rect.setLeft(o.rect.left() + fav);
    }
    o.state &= ~(QStyle::State_Selected | QStyle::State_HasFocus);
    o.backgroundBrush = Qt::NoBrush;
    o.palette.setColor(QPalette::Text, LightMode::qc(font));
    o.palette.setColor(QPalette::HighlightedText, LightMode::qc(font));
    const QWidget* w = option.widget;
    QStyle* style = w ? w->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &o, p, w);
}

// One bar across all columns; each cell draws its part of it.
void GamesListDelegate::paintSelectionBar(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex& index) const {
    QModelIndex src = filter_->mapToSource(index);
    const MameGame& g = model_->game(src.row());
    bool active = view_->hasFocus() && view_->window()->isActiveWindow();
    bool miss = g.setStatus == 1; // "Games With Missing ROMs/CHDs"
    int top = active ? (miss ? lm_.selMissTop : lm_.selTop) : lm_.selInactiveTop;
    int bottom = active ? (miss ? lm_.selMissBottom : lm_.selBottom) : lm_.selInactiveBottom;
    int frame = active ? (miss ? lm_.selMissFrame : lm_.selFrame) : lm_.selInactiveFrame;
    QHeaderView* h = view_->header();
    QRectF bar(-h->offset() + 0.5, o.rect.top() + 0.5, h->length() - 1.0, o.rect.height() - 1.0);
    QPainterPath path;
    if (lm_.selRoundCorners) path.addRoundedRect(bar, 4, 4);
    else path.addRect(bar);
    int alpha = lm_.selAlphaBlend ? 200 : 255;
    QColor ct = LightMode::qc(top), cb = LightMode::qc(bottom);
    ct.setAlpha(alpha);
    cb.setAlpha(alpha);
    p->save();
    p->setClipRect(o.rect);
    p->setRenderHint(QPainter::Antialiasing, lm_.selRoundCorners);
    if (lm_.selGradient) {
        QLinearGradient grad(bar.topLeft(), bar.bottomLeft());
        grad.setColorAt(0, ct);
        grad.setColorAt(1, cb);
        p->fillPath(path, grad);
    } else {
        p->fillPath(path, ct);
    }
    p->setPen(LightMode::qc(frame));
    p->drawPath(path);
    p->restore();
}

} // namespace eld
