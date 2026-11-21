#include "ThumbnailSettingsDialog.h"

#include "GamesModel.h"
#include "ElWidgets.h"
#include "SettingsLayout.h"

#include <QCheckBox>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace eld {

ThumbnailSettingsDialog::ThumbnailSettingsDialog(const Input& in, QWidget* parent) : QDialog(parent), in_(in) {
    setObjectName("FormThumbnailViewSettings");
    setWindowTitle(tr("Thumbnail Settings"));
    using namespace settingsui;
    auto check = [](const char* name, const QString& text, const QString& tip = {}) {
        auto* c = new QCheckBox(text);
        c->setObjectName(name);
        c->setToolTip(tip);
        return c;
    };
    auto button = [](const char* name, const QString& text, const QString& tip = {}) {
        auto* b = new QPushButton(text);
        b->setObjectName(name);
        b->setToolTip(tip);
        b->setAutoDefault(false);
        return b;
    };

    // the preview: the games list's games, with these settings
    preview_ = new GamesIconView(in.model, in.filter, this);
    preview_->setObjectName("ThumbnailPreview");
    preview_->setLook(in.look);
    preview_->setFavoriteIcon(in.favoriteIcon);
    preview_->setSpecialIcon(in.specialIcon);
    preview_->findSnapshot = in.findSnapshot;
    preview_->setMode(GamesIconView::Thumbnails);
    preview_->setMinimumHeight(240);

    // the cells' size: a slider with arrows a pixel at a time, and its value
    auto size = [&](Group& g, const QString& label, QSlider*& slider, QLabel*& value, const char* name) {
        slider = new QSlider(Qt::Horizontal);
        slider->setObjectName(name);
        slider->setRange(120, 400);
        value = new QLabel;
        value->setMinimumWidth(value->fontMetrics().horizontalAdvance(tr("%1 pixels").arg(400)) + 4);
        auto* less = button("", QStringLiteral("◀"), tr("A pixel smaller"));
        auto* more = button("", QStringLiteral("▶"), tr("A pixel bigger"));
        for (QPushButton* b : {less, more}) b->setFixedWidth(b->sizeHint().height() + 4);
        connect(less, &QPushButton::clicked, slider, [slider] { slider->setValue(slider->value() - 1); });
        connect(more, &QPushButton::clicked, slider, [slider] { slider->setValue(slider->value() + 1); });
        g.row(new QLabel(label), slider, {less, more, value});
    };
    Group cells(tr("Cell Size"), "ThumbnailCellSizeBox");
    size(cells, tr("Width"), width_, widthValue_, "GridWidthSize");
    size(cells, tr("Height"), height_, heightValue_, "GridHeightSize");
    imageSize_ = new QLabel;
    imageSize_->setObjectName("LabelImageSizeValue");
    auto* gridDefault = button("ButtonGridDefault", tr("Default Size"), tr("The cells' size as it was at first"));
    cells.line({imageSize_}, gridDefault);

    Group look(tr("Thumbnails"), "ThumbnailLookBox");
    showTitles_ = check("ShowGameTitles", tr("Show the Game Title"));
    keepAspect_ = check("MaintainAspectRatio", tr("Keep the Picture's Proportions"), tr("The pictures aren't stretched to the cell"));
    showBorder_ = check("ShowBorder", tr("Border"), tr("A border around the picture"));
    borderColor_ = new ElColorBox;
    borderColor_->setObjectName("BorderColor");
    auto* borderDefault = button("BorderColorDefault", tr("Default"), tr("The border's color as it was at first"));
    hideImages_ = check("HideThumbnailImages", tr("Hide the Pictures in the Preview"), tr("To see the overlay icons on their own"));
    look.check(showTitles_).check(keepAspect_).row(showBorder_, borderColor_, {borderDefault}).check(hideImages_);

    Group icons(tr("Overlay Icons"), "OverlayIconsGroupBox");
    showGameIcon_ = check("ShowGameIcon", tr("Game"));
    showFavoriteIcon_ = check("ShowFavoriteIcon", tr("Favorite"));
    showSpecialIcon_ = check("ShowSpecialIcon", tr("Special List"));
    iconWhenNoImage_ = check("ShowIconsWithNoThumbnail", tr("Also on Games Without a Picture"));
    leftAlign_ = new QRadioButton(tr("Top Left"));
    leftAlign_->setObjectName("ThumbLeftAlignIcons");
    rightAlign_ = new QRadioButton(tr("Top Right"));
    rightAlign_->setObjectName("ThumbRightAlignIcons");
    iconSize_ = new QComboBox;
    iconSize_->setObjectName("MediaTypeIconSize");
    iconSize_->addItems({tr("Medium (24×24)"), tr("Large (32×32)"), tr("Extra Large (48×48)")});
    icons.line({showGameIcon_, showFavoriteIcon_, showSpecialIcon_})
        .check(iconWhenNoImage_)
        .row(new QLabel(tr("Where")), leftAlign_, {rightAlign_})
        .row(new QLabel(tr("Size")), iconSize_);

    auto* apply = button("ButtonApply", tr("Show in the Games List Now"), tr("The games list's thumbnails with these settings, before OK"));
    look.line({}, apply);

    Page page("ThumbnailsPage", QString(), 3);
    page.add(0, cells).add(1, look).add(2, icons).finish();
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    auto* top = new QWidget;
    auto* topLay = new QVBoxLayout(top);
    topLay->setContentsMargins(10, 10, 10, 0);
    topLay->addWidget(preview_);
    lay->addWidget(top, 1);
    lay->addWidget(page.widget());
    // (on its own: OK and Cancel; in the Settings window they're the window's)
    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(10, 0, 10, 10);
    buttons->addStretch();
    auto* ok = button("ButtonConfirm", tr("OK"));
    auto* cancel = button("ButtonAbort", tr("Cancel"));
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    lay->addLayout(buttons);

    auto changed = [this] { if (!loading_) preview(); };
    for (QCheckBox* c : findChildren<QCheckBox*>()) connect(c, &QCheckBox::toggled, this, changed);
    for (QRadioButton* r : {leftAlign_, rightAlign_}) connect(r, &QRadioButton::toggled, this, changed);
    for (QSlider* sl : {width_, height_}) connect(sl, &QSlider::valueChanged, this, changed);
    connect(iconSize_, &QComboBox::currentIndexChanged, this, changed);
    connect(borderColor_, &ElColorBox::colorChanged, this, changed);
    connect(borderDefault, &QPushButton::clicked, this, [this] { borderColor_->setTColor(ThumbnailSettings().borderColor); });
    connect(gridDefault, &QPushButton::clicked, this, [this] {
        width_->setValue(ThumbnailSettings().width);
        height_->setValue(ThumbnailSettings().height);
    });
    connect(apply, &QPushButton::clicked, this, [this] { if (in_.apply) in_.apply(settings()); });
    connect(ok, &QPushButton::clicked, this, [this] {
        if (in_.apply) in_.apply(settings());
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, [this] {
        if (in_.apply) in_.apply(in_.settings); // as it was
        reject();
    });
    setSettings(in.settings);
}

ThumbnailSettingsDialog::~ThumbnailSettingsDialog() = default;

void ThumbnailSettingsDialog::setSettings(const ThumbnailSettings& s) {
    loading_ = true;
    width_->setValue(s.width);
    height_->setValue(s.height);
    showBorder_->setChecked(s.showBorder);
    borderColor_->setTColor(s.borderColor);
    showTitles_->setChecked(s.showTitle);
    keepAspect_->setChecked(s.keepAspect);
    showGameIcon_->setChecked(s.showGameIcon);
    showFavoriteIcon_->setChecked(s.showFavoriteIcon);
    showSpecialIcon_->setChecked(s.showSpecialIcon);
    leftAlign_->setChecked(s.leftAlignIcons);
    rightAlign_->setChecked(!s.leftAlignIcons);
    iconWhenNoImage_->setChecked(s.iconWhenNoImage);
    iconSize_->setCurrentIndex(std::clamp(s.mediaTypeIconSize, 0, iconSize_->count() - 1));
    loading_ = false;
    preview();
}

ThumbnailSettings ThumbnailSettingsDialog::settings() const {
    ThumbnailSettings s = in_.settings; // keeps what this window doesn't show
    s.width = width_->value();
    s.height = height_->value();
    s.showBorder = showBorder_->isChecked();
    s.borderColor = borderColor_->tcolor();
    s.showTitle = showTitles_->isChecked();
    s.keepAspect = keepAspect_->isChecked();
    s.showGameIcon = showGameIcon_->isChecked();
    s.showFavoriteIcon = showFavoriteIcon_->isChecked();
    s.showSpecialIcon = showSpecialIcon_->isChecked();
    s.leftAlignIcons = leftAlign_->isChecked();
    s.iconWhenNoImage = iconWhenNoImage_->isChecked();
    s.mediaTypeIconSize = iconSize_->currentIndex();
    return s;
}

void ThumbnailSettingsDialog::preview() {
    ThumbnailSettings s = settings();
    widthValue_->setText(tr("%1 pixels").arg(s.width));
    heightValue_->setText(tr("%1 pixels").arg(s.height));
    const QSize img = GamesIconView::imageArea(s, preview_->font()).size();
    imageSize_->setText(tr("Picture: %1\u00D7%2 pixels").arg(img.width()).arg(img.height()));
    preview_->setHideImages(hideImages_->isChecked());
    preview_->setThumbnails(s);
}

} // namespace eld
