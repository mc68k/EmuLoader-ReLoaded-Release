// ThumbnailSettingsDialog - Thumbnail Settings > Customize (TFormThumbnailViewSettings): the
// thumbnails' cell size, border, titles, aspect ratio and overlay icons, with a preview of the
// games list as it will look. Apply shows it in the list at once; Ok keeps it
// (games_filters.ini [Thumbnails]); Cancel goes back to what was there.
#pragma once

#include "GamesIconView.h"

#include <QDialog>
#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QRadioButton;
class QSlider;

namespace eld {

class ThumbnailSettingsDialog : public QDialog {
    Q_OBJECT
public:
    struct Input {
        ThumbnailSettings settings;
        GamesModel* model = nullptr;
        GamesFilter* filter = nullptr;
        LightMode look;
        QIcon favoriteIcon, specialIcon;
        std::function<ImageFinder::Found(const QString&, const QString&)> findSnapshot;
        std::function<void(const ThumbnailSettings&)> apply; // the games list, now
    };
    explicit ThumbnailSettingsDialog(const Input& in, QWidget* parent = nullptr);
    ~ThumbnailSettingsDialog() override;
    ThumbnailSettings settings() const;
    void setSettings(const ThumbnailSettings& s);

private:
    void preview();
    Input in_;
    GamesIconView* preview_;
    bool loading_ = false;
    QSlider *width_, *height_;
    QLabel *widthValue_, *heightValue_, *imageSize_;
    QCheckBox *showBorder_, *showTitles_, *keepAspect_, *hideImages_;
    QCheckBox *showGameIcon_, *showFavoriteIcon_, *showSpecialIcon_, *iconWhenNoImage_;
    class ElColorBox* borderColor_;
    QRadioButton *leftAlign_, *rightAlign_;
    QComboBox* iconSize_;
};

} // namespace eld
