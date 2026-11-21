// GamesIconView - the games list's other views (ButtonViewMode): Small Icons, Large Icons, Tiles
// and Thumbnails, over the same games (GamesFilter) and the same selection as the details list,
// which stays the program's list for everything else: only the drawing changes.
//
//   Small Icons   a 16x16 icon and the title, in rows
//   Large Icons   a 32x32 icon over the title
//   Tiles         a large icon beside the title and the year, manufacturer and set name, four
//                 sizes (Tiles View Cell Size, [Preferences] TilesViewCellSize)
//   Thumbnails    the in-game snapshot (or the parent's), the title under it, and the game,
//                 favorite and Special List icons; games_filters.ini [Thumbnails] (Thumbnail
//                 Settings > Customize)
#pragma once

#include "ImageFinder.h"
#include "LightMode.h"

#include <QCache>
#include <QIcon>
#include <QImage>
#include <QListView>
#include <QSet>
#include <QStyledItemDelegate>
#include <atomic>
#include <functional>
#include <memory>

namespace eld {

class GamesModel;
class GamesFilter;

// games_filters.ini [Thumbnails] (ReadThumbnailSettings / WriteThumbnailSettings)
struct ThumbnailSettings {
    int width = 144, height = 178;   // WidthSize, HeightSize: the cell
    bool showBorder = false;         // ShowBorder, BorderColor
    int borderColor = 10902593;      // $00a65c41
    bool showParent = true;          // ShowParent: the parent's snapshot for a clone without one
    bool showTitle = true;           // ShowTitle
    bool keepAspect = true;          // MaintainAspectRatio
    bool showGameIcon = true;        // GameIconVisible
    bool showFavoriteIcon = true;    // FavoriteIconVisible
    bool showSpecialIcon = true;     // SpecialIconVisible
    bool leftAlignIcons = true;      // LeftAlignIcons
    bool iconWhenNoImage = true;     // ShowIconInNoThumbnail
    // kept as they are (the console/computer systems' icons)
    int systemIconSize = 1, mediaTypeIconSize = 0;
    bool showSystemIcon = true, showMediaTypeIcon = true;
    void load(const QString& file);
    void save(const QString& file) const;
};

class GamesIconView : public QListView {
    Q_OBJECT
public:
    enum Mode { SmallIcons = 2, LargeIcons = 3, Tiles = 4, Thumbnails = 5 };
    GamesIconView(GamesModel* model, GamesFilter* filter, QWidget* parent = nullptr);

    void setMode(int mode);
    int mode() const { return mode_; }
    void setTileSize(int tag);             // 0 = 128x128 icons ... 3 = 48x48 (PopupTilesViewGameIconsSize)
    int tileSize() const { return tileSize_; }
    void setThumbnails(const ThumbnailSettings& s);
    const ThumbnailSettings& thumbnails() const { return thumbs_; }
    void setLook(const LightMode& lm);
    void setFavoriteIcon(const QIcon& i) { favIcon_ = i; viewport()->update(); }
    void setSpecialIcon(const QIcon& i) { specialIcon_ = i; viewport()->update(); }
    // where a game's snapshot is (ImageFinder, on this thread); it's read and shrunk on another
    std::function<ImageFinder::Found(const QString& name, const QString& parent)> findSnapshot;
    void reloadThumbnails(bool selectedOnly = false);
    QSize cellSize() const;
    // the picture's place in a thumbnail cell (at 0,0), under which the title goes
    static QRect imageArea(const ThumbnailSettings& s, const QFont& f);
    void setHideImages(bool on) { hideImages_ = on; viewport()->update(); } // the settings' preview
    int thumbnailsLoaded() const { return cache_.size(); }

protected:
    void scrollContentsBy(int dx, int dy) override;

private:
    friend class GamesIconDelegate;
    const QImage* thumbnail(int sourceRow);
    void thumbnailReady(const QString& name, const QImage& img);

    GamesModel* model_;
    GamesFilter* filter_;
    int mode_ = LargeIcons;
    int tileSize_ = 3;
    ThumbnailSettings thumbs_;
    LightMode lm_;
    QIcon favIcon_, specialIcon_;
    QCache<QString, QImage> cache_{800};
    QSet<QString> pending_, none_;
    int generation_ = 0;
    // a step for each scroll: a thumbnail not read yet when the view moves on is dropped (asked
    // again if it's still in sight), rather than read before the ones now shown
    std::shared_ptr<std::atomic<int>> scrolled_ = std::make_shared<std::atomic<int>>(0);
    bool hideImages_ = false;
};

} // namespace eld
