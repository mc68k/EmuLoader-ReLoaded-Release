#include "SplashCustomizeDialog.h"
#include "FileProblem.h"

#include "ElWidgets.h"
#include "SettingsLayout.h"
#include "SplashView.h"

#include <QCheckBox>
#include <QDir>
#include <QRandomGenerator>
#include <tuple>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSlider>
#include <QVBoxLayout>

namespace eld {

SplashCustomizeDialog::SplashCustomizeDialog(const QString& imagesFolder, const QString& iniFile, QWidget* parent)
    : QDialog(parent), iniFile_(iniFile), s_(SplashSettings::load(iniFile)) {
    setWindowTitle(tr("Customize Splash Screen"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    auto* top = new QHBoxLayout(this);

    // the splash screen, with the texts the original shows in this mode
    view_ = new SplashView(imagesFolder, s_, this);
    QScreen* sc = parent ? parent->screen() : QGuiApplication::primaryScreen();
    double scale = 1.0;
    if (sc && sc->availableGeometry().width() < SplashView::ArtW + 520) scale = (sc->availableGeometry().width() - 520) / double(SplashView::ArtW);
    const double most = std::clamp(scale, 0.5, 1.0);
    view_->setScale(most);
    view_->setVersionText(QStringLiteral(EL_PRODUCT_NAME " " EL_VERSION_LABEL));
    view_->setTitle(tr("Create Arcade Games List"));
    view_->setMessage(tr("Saving the games list (%1 machines)...").arg(QLocale().toString(47821)));
    view_->setCount(tr("%1 of %2").arg("012", "100"));
    view_->setProgress(55);
    view_->setTimer(83000);
    view_->setEditable(true);
    view_->setToolTip(tr("Drag the version to move it"));
    auto* left = new QVBoxLayout;
    left->addWidget(view_);
    left->addStretch();
    top->addLayout(left);

    auto* panel = new QVBoxLayout;
    top->addLayout(panel);
    // in the Settings window: the preview as big as the room beside the settings allows, or
    // above them across the page when it would be small beside them
    settingsui::onRoom(this, [this, top, panel, most](int width) {
        const QMargins m = top->contentsMargins();
        const int across = width - m.left() - m.right();
        const int beside = across - top->spacing() - panel->minimumSize().width();
        const bool side = beside >= SplashView::ArtW * 0.55;
        const double scale = std::clamp((side ? beside : across) / double(SplashView::ArtW), 0.3, most);
        const auto dir = side ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom;
        if (top->direction() == dir && std::abs(scale - view_->scale()) < 0.005) return false;
        top->setDirection(dir);
        view_->setScale(scale);
        return true;
    });
    auto color = [this] {
        auto* c = new ElColorBox(this);
        c->setMinimumWidth(150);
        connect(c, &ElColorBox::colorChanged, this, &SplashCustomizeDialog::changed);
        return c;
    };
    auto check = [this](const QString& text) {
        auto* c = new QCheckBox(text, this);
        connect(c, &QCheckBox::toggled, this, &SplashCustomizeDialog::changed);
        return c;
    };
    auto slider = [this](int max, QLabel*& value) {
        auto* s = new QSlider(Qt::Horizontal, this);
        s->setRange(0, max);
        value = new QLabel(this);
        value->setMinimumWidth(QFontMetrics(value->font()).horizontalAdvance("0000"));
        connect(s, &QSlider::valueChanged, this, &SplashCustomizeDialog::changed);
        return s;
    };

    // Logo Image Settings
    auto* logo = new QGroupBox(tr("Logo Image Settings"), this);
    auto* lg = new QGridLayout(logo);
    altLogo_ = check(tr("Alternate Logo"));
    altLogo_->setToolTip(tr("resources\\images\\logo2.png (the Night Mode Edition artwork)"));
    altLayout_ = check(tr("Alternate Text Layout"));
    altLayout_->setToolTip(tr("The title beside the timer, the progress bar above it"));
    large_ = check(tr("Ultra Size (1.5x)"));
    large_->setToolTip(tr("The splash screen half as big again (the original's 4K Ultra Size x2)"));
    lg->addWidget(altLogo_, 0, 0);
    lg->addWidget(altLayout_, 0, 1);
    lg->addWidget(large_, 1, 0);
    panel->addWidget(logo);

    // Startup Effect (new)
    auto* fx = new QGroupBox(tr("Startup Effect"), this);
    auto* fg = new QGridLayout(fx);
    effect_ = new QComboBox(this);
    for (const QString& k : QStringList{"none", "random"} + SplashSettings::effectKeys())
        effect_->addItem(SplashSettings::effectTitle(k), k);
    effect_->setToolTip(tr("Played when EmuLoader starts. A click or a key skips it."));
    connect(effect_, &QComboBox::currentIndexChanged, this, &SplashCustomizeDialog::changed);
    insertCoin_ = check(tr("Insert Coin"));
    insertCoin_->setToolTip(tr("A blinking INSERT COIN after the effect (with any effect, or none)"));
    auto* preview = new QPushButton(tr("Preview"), this);
    preview->setAutoDefault(false);
    connect(preview, &QPushButton::clicked, this, [this] {
        QString e = s_.effect;
        if (e == "random") {
            QStringList keys = SplashSettings::effectKeys();
            e = keys.at(int(QRandomGenerator::global()->bounded(keys.size())));
        }
        view_->setEditable(false);
        view_->play(e, s_.insertCoin);
    });
    connect(view_, &SplashView::effectFinished, this, [this] { view_->setEditable(true); });
    connect(view_, &SplashView::skipped, this, [this] { view_->setEditable(true); });
    fg->addWidget(new QLabel(tr("Effect"), this), 0, 0);
    fg->addWidget(effect_, 0, 1);
    fg->addWidget(preview, 0, 2);
    fg->addWidget(insertCoin_, 1, 1);
    panel->addWidget(fx);

    // Text Font Colors
    auto* tc = new QGroupBox(tr("Text Font Colors"), this);
    auto* tg = new QGridLayout(tc);
    shadows_ = check(tr("Enable Shadows"));
    oneColor_ = check(tr("One Color"));
    oneColor_->setToolTip(tr("Use one shadow color for all the texts, instead of a color for each text."));
    tg->addWidget(shadows_, 0, 0, 1, 2);
    tg->addWidget(oneColor_, 0, 2, 1, 2);
    shadowAll_ = color();
    tg->addWidget(new QLabel(tr("Shadows"), this), 1, 0);
    tg->addWidget(shadowAll_, 1, 1);
    distance_ = slider(10, distanceV_);
    const QString distTip = tr("How far each shadow is from its text, down and to the right, in pixels.\n"
                               "0: no shadow. 1 (the default) gives a thin outline that keeps the texts\n"
                               "readable over the picture; more gives a deeper, drop-shadow look.");
    distance_->setToolTip(distTip);
    auto* distLabel = new QLabel(tr("Distance"), this);
    distLabel->setToolTip(distTip);
    tg->addWidget(distLabel, 1, 2);
    auto* dist = new QHBoxLayout;
    dist->addWidget(distance_);
    dist->addWidget(distanceV_);
    tg->addLayout(dist, 1, 3);
    const QString names[] = {tr("Title"), tr("Message"), tr("Timer"), tr("Software"), tr("Version")};
    const QString tips[] = {tr("The job, such as Create Arcade Games List"), tr("What it's doing now"),
                            tr("The time since it started"), tr("The count, such as 012 of 100"),
                            tr("The program's name and version")};
    for (int t = 0; t < SplashSettings::TextCount; ++t) {
        text_[t] = color();
        shadow_[t] = color();
        auto* l = new QLabel(names[t], this);
        l->setToolTip(tips[t]);
        tg->addWidget(l, 2 + t, 0);
        tg->addWidget(text_[t], 2 + t, 1);
        tg->addWidget(new QLabel(tr("Shadow"), this), 2 + t, 2);
        tg->addWidget(shadow_[t], 2 + t, 3);
    }
    versionX_ = slider(SplashView::ArtW, versionXV_);
    versionY_ = slider(SplashView::ArtH, versionYV_);
    // moved by hand: a place of its own (the default, -1, keeps it against the right edge)
    for (QSlider* sl : {versionX_, versionY_})
        connect(sl, &QSlider::valueChanged, this, [this, sl](int v) {
            if (filling_) return;
            (sl == versionX_ ? s_.versionX : s_.versionY) = v;
            changed();
        });
    for (auto [label, s, v, row] : {std::tuple{tr("Version X"), versionX_, versionXV_, 7}, std::tuple{tr("Version Y"), versionY_, versionYV_, 8}}) {
        tg->addWidget(new QLabel(label, this), row, 0);
        auto* h = new QHBoxLayout;
        h->addWidget(s);
        h->addWidget(v);
        tg->addLayout(h, row, 1, 1, 3);
    }
    connect(view_, &SplashView::versionMoved, this, [this](int x, int y) {
        filling_ = true;
        versionX_->setValue(x);
        versionY_->setValue(y);
        versionXV_->setNum(x);
        versionYV_->setNum(y);
        s_.versionX = x;
        s_.versionY = y;
        filling_ = false;
    });
    panel->addWidget(tc);

    // Progress bar
    auto* pb = new QGroupBox(tr("Progress Bar"), this);
    auto* pg = new QGridLayout(pb);
    scheme_ = new QComboBox(this);
    scheme_->addItems(SplashSettings::schemeNames());
    connect(scheme_, &QComboBox::currentIndexChanged, this, &SplashCustomizeDialog::changed);
    copyColors_ = new QPushButton(tr("Copy Current Colors To Custom"), this);
    copyColors_->setAutoDefault(false);
    connect(copyColors_, &QPushButton::clicked, this, [this] { // ProgressBarCopyCurrentColorsToCustomButtonClick
        s_.custom = s_.progressColors();
        s_.scheme = 0;
        show(s_);
        changed();
    });
    pg->addWidget(new QLabel(tr("Progress"), this), 0, 0);
    pg->addWidget(scheme_, 0, 1);
    pg->addWidget(copyColors_, 0, 2, 1, 2);
    backTop_ = color(); backDown_ = color(); border_ = color(); barTop_ = color(); barDown_ = color();
    backTopL_ = new QLabel(tr("Background Top"), this);
    backDownL_ = new QLabel(tr("Background Bottom"), this);
    borderL_ = new QLabel(tr("Border"), this);
    barTopL_ = new QLabel(tr("Bar Top"), this);
    barDownL_ = new QLabel(tr("Bar Bottom"), this);
    backTopL_->setToolTip(tr("The bar's background, top"));
    backDownL_->setToolTip(tr("The bar's background, bottom"));
    borderL_->setToolTip(tr("The bar's border"));
    barTopL_->setToolTip(tr("The bar, top"));
    barDownL_->setToolTip(tr("The bar, bottom"));
    pg->addWidget(backTopL_, 1, 0); pg->addWidget(backTop_, 1, 1);
    pg->addWidget(barTopL_, 1, 2); pg->addWidget(barTop_, 1, 3);
    pg->addWidget(backDownL_, 2, 0); pg->addWidget(backDown_, 2, 1);
    pg->addWidget(barDownL_, 2, 2); pg->addWidget(barDown_, 2, 3);
    pg->addWidget(borderL_, 3, 0); pg->addWidget(border_, 3, 1);
    panel->addWidget(pb);
    // names (Find a Setting's targets)
    effect_->setObjectName("SplashEffect");
    distance_->setObjectName("SplashShadowDistance");
    shadowAll_->setObjectName("SplashShadowColor");
    versionX_->setObjectName("SplashVersionX");
    versionY_->setObjectName("SplashVersionY");
    scheme_->setObjectName("SplashProgressScheme");
    const char* textNames[] = {"Title", "Message", "Timer", "Software", "Version"};
    for (int t = 0; t < SplashSettings::TextCount; ++t) {
        text_[t]->setObjectName(QString("Splash%1Color").arg(textNames[t]));
        shadow_[t]->setObjectName(QString("Splash%1ShadowColor").arg(textNames[t]));
    }
    for (auto [box, name] : {std::pair{backTop_, "SplashProgressBackgroundTop"}, std::pair{backDown_, "SplashProgressBackgroundBottom"},
                             std::pair{border_, "SplashProgressBorder"}, std::pair{barTop_, "SplashProgressBarTop"},
                             std::pair{barDown_, "SplashProgressBarBottom"}})
        box->setObjectName(name);

    // the Default buttons (ColorsBoxButtonDefault, VersionInfoPositionButtonDefault,
    // ProgressBarSchemeButtonDefault), OK and Cancel
    auto* defaults = new QHBoxLayout;
    defaults->addWidget(new QLabel(tr("Default:"), this));
    auto button = [&](const QString& text, const QString& tip, auto fn) {
        auto* b = new QPushButton(text, this);
        b->setToolTip(tip);
        b->setAutoDefault(false);
        connect(b, &QPushButton::clicked, this, fn);
        defaults->addWidget(b);
    };
    button(tr("Colors"), tr("The texts' default colors"), [this] {
        for (int t = 0; t < SplashSettings::TextCount; ++t) s_.textColor[t] = SplashSettings::defaultTextColor(t);
        show(s_);
        changed();
    });
    button(tr("Version"), tr("The version's default place"), [this] {
        s_.versionX = SplashSettings::defaultVersionX;
        s_.versionY = SplashSettings::defaultVersionY;
        show(s_);
        changed();
    });
    button(tr("Progress"), tr("The progress bar's default colors (Desert)"), [this] {
        s_.scheme = 1;
        show(s_);
        changed();
    });
    defaults->addStretch();
    panel->addLayout(defaults);
    panel->addStretch();
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    panel->addWidget(box);

    show(s_);
    changed();
}

void SplashCustomizeDialog::show(const SplashSettings& s) {
    filling_ = true;
    altLogo_->setChecked(s.alternateLogo);
    altLayout_->setChecked(s.alternateLayout);
    large_->setChecked(s.largeSize);
    effect_->setCurrentIndex(std::max(0, effect_->findData(s.effect)));
    insertCoin_->setChecked(s.insertCoin);
    shadows_->setChecked(s.shadows);
    oneColor_->setChecked(s.oneShadowColor);
    shadowAll_->setTColor(s.shadowColorAll);
    distance_->setValue(s.shadowDistance);
    for (int t = 0; t < SplashSettings::TextCount; ++t) {
        text_[t]->setTColor(s.textColor[t]);
        shadow_[t]->setTColor(s.shadowColor[t]);
    }
    {
        SplashSettings shown = s; // where it is now (x = -1: worked out from the text's width)
        view_->setSettings(shown);
        QRect vr = view_->versionRect();
        versionX_->setValue(vr.x());
        versionY_->setValue(vr.y());
    }
    scheme_->setCurrentIndex(std::clamp(s.scheme, 0, scheme_->count() - 1));
    filling_ = false;
    updateProgressBoxes();
}

// SetProgressBarColorBox / ToggleProgressBarColorBox: the scheme's colors, editable for Custom Colors only.
void SplashCustomizeDialog::updateProgressBoxes() {
    filling_ = true;
    const ProgressColors c = s_.progressColors();
    backTop_->setTColor(c.backFace);
    backDown_->setTColor(c.backGrad);
    border_->setTColor(c.border);
    barTop_->setTColor(c.foreFace);
    barDown_->setTColor(c.foreGrad);
    const bool custom = s_.scheme == 0;
    for (QWidget* w : {(QWidget*)backTop_, (QWidget*)backDown_, (QWidget*)border_, (QWidget*)barTop_, (QWidget*)barDown_,
                       (QWidget*)backTopL_, (QWidget*)backDownL_, (QWidget*)borderL_, (QWidget*)barTopL_, (QWidget*)barDownL_})
        w->setEnabled(custom);
    copyColors_->setEnabled(!custom);
    filling_ = false;
}

void SplashCustomizeDialog::changed() {
    if (filling_) return;
    const int oldScheme = s_.scheme;
    s_.alternateLogo = altLogo_->isChecked();
    s_.alternateLayout = altLayout_->isChecked();
    s_.largeSize = large_->isChecked();
    s_.effect = effect_->currentData().toString();
    s_.insertCoin = insertCoin_->isChecked();
    s_.shadows = shadows_->isChecked();
    s_.oneShadowColor = oneColor_->isChecked();
    s_.shadowColorAll = shadowAll_->tcolor();
    s_.shadowDistance = distance_->value();
    distanceV_->setNum(s_.shadowDistance);
    shadowAll_->setEnabled(s_.oneShadowColor);
    for (int t = 0; t < SplashSettings::TextCount; ++t) {
        s_.textColor[t] = text_[t]->tcolor();
        s_.shadowColor[t] = shadow_[t]->tcolor();
        shadow_[t]->setEnabled(!s_.oneShadowColor);
    }
    view_->setSettings(s_);
    versionXV_->setNum(view_->versionRect().x());
    versionYV_->setNum(view_->versionRect().y());
    s_.scheme = scheme_->currentIndex();
    if (s_.scheme == 0 && oldScheme == 0) { // the custom colors as edited
        s_.custom = {border_->tcolor(), backTop_->tcolor(), backDown_->tcolor(), barTop_->tcolor(), barDown_->tcolor()};
    }
    if (s_.scheme != oldScheme) updateProgressBoxes();
    view_->setSettings(s_);
}

void SplashCustomizeDialog::accept() {
    if (!s_.save(iniFile_)) {
        FileProblem::cantSave(this, windowTitle(), iniFile_);
        return;
    }
    QDialog::accept();
}

} // namespace eld
