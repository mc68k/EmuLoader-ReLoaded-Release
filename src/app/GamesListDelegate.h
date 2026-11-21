// GamesListDelegate - draws the games list selection bar like TEasyListview with EmuLoader's
// light mode settings: a vertical gradient (or solid) bar across the whole row, with a frame,
// optional round corners and alpha blending; its own colors for sets with missing ROMs and
// for when the list doesn't have the focus.
#pragma once

#include "LightMode.h"

#include <QIcon>
#include <QStyledItemDelegate>

class QTreeView;

namespace eld {

class GamesModel;
class GamesFilter;

class GamesListDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    GamesListDelegate(QTreeView* view, GamesModel* model, GamesFilter* filter);
    void setLook(const LightMode& lm) { lm_ = lm; }
    void setFavoriteIcon(const QIcon& i) { favIcon_ = i; }
    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    // Grouped view: the group's title above its first row; clones indented (!DisableCloneIndent)
    void setCloneIndent(bool on) { cloneIndent_ = on; }
    // Game Multiline Captions: titles on two lines when the rows are tall enough (Large icons and up)
    void setMultiline(bool on) { multiline_ = on; }
    // A caption in two lines when it's wider than `option`'s text room and the row has room for
    // two lines (the second one elided); false when it stays as it is
    static bool twoLineCaption(QStyleOptionViewItem* option, int minIconSize);
    static constexpr int GroupBand = 26;
    // Night Mode's colors for the group titles (else the light ones)
    void setGroupColors(const QColor& text, const QColor& line) { groupText_ = text; groupLine_ = line; }

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override;

private:
    QTreeView* view_;
    GamesModel* model_;
    GamesFilter* filter_;
    LightMode lm_;
    QIcon favIcon_;
    bool cloneIndent_ = true, multiline_ = false;
    QColor groupText_ = QColor(0x1e, 0x39, 0x87), groupLine_ = QColor(0xa0, 0xb8, 0xdc);
    int band(const QModelIndex& index) const;
    void paintGroup(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const;
    void paintCell(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const;
    // Show Favorite Icon in Games: room for the star before the Title column's icon (the
    // original's state image), except while the favorites list is shown.
    int favoriteSpace(const QModelIndex& index) const;
    void paintFavorite(QPainter* p, const QRect& cell, const QModelIndex& index) const;
    void paintSelectionBar(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex& index) const;
};

// Game Multiline Captions for the Machines List panel (its biggest icons only, as the original)
class MultilineDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    bool on = false;
    int minIconSize = 68;
    int column = 0;

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        if (on && index.column() == column) GamesListDelegate::twoLineCaption(option, minIconSize);
    }
};

} // namespace eld
