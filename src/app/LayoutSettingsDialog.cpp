#include "LayoutSettingsDialog.h"
#include "ImageFinder.h"
#include "SettingsLayout.h"
#include "VclRuntime.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QPushButton>
#include <QRegularExpression>
#include "WinIni.h"
#include "ui_FormImageLayoutSettings.h"

#include <QEvent>
#include <QLabel>
#include <QApplication>
#include <QGridLayout>
#include <QScrollBar>
#include <QHBoxLayout>
#include <QListView>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QStandardItemModel>

namespace eld {

LayoutSettingsDialog::LayoutSettingsDialog(const QString& layoutsFile, const QString& resourcesFolder, int currentLayout,
                                           std::function<int(int)> pickCategory, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormImageLayoutSettings), file_(layoutsFile), res_(resourcesFolder),
      current_(currentLayout), pick_(std::move(pickCategory)) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("Image Layout Settings"));

    icons_ = {ui_->IconLayScr1, ui_->IconLayScr2, ui_->IconLayScr3, ui_->IconLayScr4};
    labels_ = {ui_->LabelLayScr1, ui_->LabelLayScr2, ui_->LabelLayScr3, ui_->LabelLayScr4};
    iconsCC_ = {ui_->IconLayScr1_ConsComp, ui_->IconLayScr2_ConsComp, ui_->IconLayScr3_ConsComp, ui_->IconLayScr4_ConsComp};
    labelsCC_ = {ui_->LabelLayScr1_ConsComp, ui_->LabelLayScr2_ConsComp, ui_->LabelLayScr3_ConsComp, ui_->LabelLayScr4_ConsComp};
    for (QLabel* l : icons_) { l->installEventFilter(this); l->setCursor(Qt::PointingHandCursor); l->setToolTip(tr("Click to choose the image category")); }
    for (QLabel* l : iconsCC_) { l->installEventFilter(this); l->setCursor(Qt::PointingHandCursor); l->setToolTip(tr("Click to choose the image category for console/computer machines")); }

    // Settings of every layout (LoadLayouts)
    WinIni ini;
    ini.load(file_);
    for (int l = 0; l < ImageLayouts::Count; ++l) s_[l] = ImageLayouts::read(ini, l, 0, 0);

    // The layouts list (an icon grid with check boxes, as the original's TEasyListView).
    auto* list = new QListView(this);
    list_ = list;
    list->viewport()->installEventFilter(this);
    list->setViewMode(QListView::IconMode);
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setIconSize(QSize(32, 32));
    list->setGridSize(QSize(104, 82));
    list->setWordWrap(true);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list->setFrameShape(QFrame::NoFrame);
    model_ = new QStandardItemModel(this);
    for (int l = 0; l < ImageLayouts::Count; ++l) {
        // "Dual [Layout 1]" -> "Dual\nLayout 1"
        QString t = layoutName(l, "\n");
        auto* it = new QStandardItem(QIcon(res_ + "main_icons/" + ImageLayouts::iconFile(l) + ".ico"), t);
        it->setData(l);
        model_->appendRow(it);
        markItem(l);
    }
    list->setModel(model_);
    connect(list->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& i) { if (i.isValid()) showLayout(model_->itemFromIndex(i)->data().toInt()); });

    // Panels 2 and 3 on / off (dual and triple layouts)
    connect(ui_->PanelEnabledScr2, &QCheckBox::toggled, this, [this](bool on) {
        if (filling_ || selected() < 1 || selected() > 8) return;
        s_[selected()].enabled[0] = on;
        showLayout(selected());
    });
    connect(ui_->PanelEnabledScr3, &QCheckBox::toggled, this, [this](bool on) {
        if (filling_ || selected() < 3 || selected() > 8) return;
        s_[selected()].enabled[1] = on;
        showLayout(selected());
    });
    // Default: the layout's default categories
    connect(ui_->ButtonLayoutResetPanelsIndex, &QPushButton::clicked, this, [this] {
        int l = selected();
        for (int i = 1; i <= ImageLayouts::imageCount(l); ++i) s_[l].category[i - 1] = ImageLayouts::defaultCategory(l, i);
        s_[l].video.fill(false);
        if (l == 0) {
            s_[0].consComp[0] = s_[0].category[0];
            s_[0].videoCC[0] = false;
        }
        showLayout(l);
    });
    connect(ui_->ButtonLayoutResetPanelsIndex_ConsComp, &QPushButton::clicked, this, [this] {
        int l = selected();
        for (int i = 1; i <= ImageLayouts::imageCount(l); ++i) s_[l].consComp[i - 1] = ImageLayouts::defaultCategory(l, i);
        s_[l].videoCC.fill(false);
        if (l == 0) {
            s_[0].category[0] = s_[0].consComp[0];
            s_[0].video[0] = false;
        }
        showLayout(l);
    });
    connect(ui_->ButtonLayoutCopyCatAllSystems_ConsComp, &QPushButton::clicked, this, [this] {
        int l = selected();
        s_[l].consComp = s_[l].category;
        s_[l].videoCC = s_[l].video;
        showLayout(l);
    });
    useCC_ = ImageLayouts::useConsComp(ini);
    connect(this, &QDialog::accepted, this, &LayoutSettingsDialog::save); // (the Settings window's OK)

    list->setCurrentIndex(model_->index(std::clamp(current_, 0, ImageLayouts::Count - 1), 0));
    showLayout(current_);
}

LayoutSettingsDialog::~LayoutSettingsDialog() = default;

QString LayoutSettingsDialog::layoutName(int l, const QString& sep) {
    // from the ini section's name: "Quad [Layout 1 Hor]"
    static const char* kinds[] = {QT_TR_NOOP("Single"), QT_TR_NOOP("Dual"), QT_TR_NOOP("Triple"), QT_TR_NOOP("Quad")};
    const QString section = ImageLayouts::section(l);
    const int n = ImageLayouts::imageCount(l);
    const QRegularExpressionMatch m = QRegularExpression(R"(\[Layout (\d+)(?: (Hor|Vert))?\])").match(section);
    if (!m.hasMatch()) return tr(kinds[n - 1]);
    QString layout = tr("Layout %1").arg(m.captured(1));
    if (m.captured(2) == "Hor") layout = tr("Layout %1 Horizontal").arg(m.captured(1));
    if (m.captured(2) == "Vert") layout = tr("Layout %1 Vertical").arg(m.captured(1));
    return tr(kinds[n - 1]) + sep + layout;
}

// A layout in the list: dimmed when the < > layout buttons skip it
void LayoutSettingsDialog::markItem(int l) {
    QStandardItem* it = model_ ? model_->item(l) : nullptr;
    if (!it) return;
    const QIcon icon(res_ + "main_icons/" + ImageLayouts::iconFile(l) + ".ico");
    it->setIcon(s_[l].visible ? icon : QIcon(icon.pixmap(32, 32, QIcon::Disabled)));
    it->setForeground(s_[l].visible ? QBrush() : QBrush(Qt::gray));
    it->setToolTip(s_[l].visible ? QString() : tr("Skipped by the < > layout buttons"));
}

// Include in the < > buttons: one layout at least
bool LayoutSettingsDialog::include(int l, bool on) {
    if (!on) {
        int left = 0;
        for (int i = 0; i < ImageLayouts::Count; ++i) left += i != l && s_[i].visible;
        if (!left) {
            QMessageBox::information(this, tr("Layouts"),
                                     tr("At least one layout has to be included in the < > layout buttons."));
            return false;
        }
    }
    s_[l].visible = on;
    touched_[l] = true;
    markItem(l);
    updateInUse();
    return true;
}

// The layout in use stays included (as in the original): switching with OK to a layout that's left
// out includes it, unless its check box was cleared here
bool LayoutSettingsDialog::autoIncluded() const {
    const int sel = selected();
    return sel != current_ && !s_[sel].visible && !touched_[sel];
}

// Include All, or Include Only This One
void LayoutSettingsDialog::includeAll(bool all) {
    const int sel = selected();
    for (int l = 0; l < ImageLayouts::Count; ++l) {
        s_[l].visible = all || l == sel;
        touched_[l] = true;
        markItem(l);
    }
    showLayout(sel);
}

// The layout in use, and the one OK switches to
void LayoutSettingsDialog::updateInUse() {
    if (!inUse_) return;
    const int sel = selected();
    inUse_->setText(sel == current_   ? tr("In use: %1").arg(layoutName(current_))
                    : autoIncluded() ? tr("In use: %1. OK switches to %2 and includes it in the < > buttons.")
                                           .arg(layoutName(current_), layoutName(sel))
                                     : tr("In use: %1. OK switches to %2.").arg(layoutName(current_), layoutName(sel)));
}

int LayoutSettingsDialog::chosen() const { return selected(); }

// The Settings window's Layouts tab: the layouts, the panels of the selected one (for all
// systems, and for consoles and computers), its preview; the Help texts under what they're about.
void LayoutSettingsDialog::settingsLayout() {
    using settingsui::Group;
    QLabel* like = new QLabel(this);
    like->setStyleSheet("color: #6E6E6E;");
    like->hide();
    auto hint = [like](const QString& t) { return settingsui::hint(t, like); };
    // the original's group boxes inside the new groups: no title or frame of their own, the panels
    // in a grid (a column each: its title, its picture, the picture's name) under the buttons
    auto tiles = [this](QGroupBox* b, bool cc, QList<QPushButton*> buttons) {
        b->setTitle(QString());
        b->setFlat(true);
        b->setStyleSheet("QGroupBox { border: 0; margin: 0; padding: 0; }");
        b->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        b->setProperty("vclLaidOut", true); // (the design's places are left behind)
        auto free = [](QWidget* w) {
            vcl::stopEliding(w); // (it has room for its whole text now)
            w->setMinimumSize(0, 0);
            w->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            w->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        };
        auto* v = new QVBoxLayout(b);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(4);
        auto* top = new QHBoxLayout;
        top->addStretch();
        for (QPushButton* p : buttons) top->addWidget(p);
        v->addLayout(top);
        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(12);
        grid->setVerticalSpacing(3);
        const QString n = cc ? QStringLiteral("_ConsComp") : QString();
        for (int i = 0; i < 4; ++i) {
            const QString k = QString::number(i + 1);
            if (auto* t = b->findChild<QLabel*>("LabelPanel" + k + n)) {
                free(t);
                t->setAlignment(Qt::AlignCenter);
                grid->addWidget(t, 0, i);
            }
            if (!cc && (i == 1 || i == 2)) {
                free(i == 1 ? ui_->PanelEnabledScr2 : ui_->PanelEnabledScr3);
                grid->addWidget(i == 1 ? ui_->PanelEnabledScr2 : ui_->PanelEnabledScr3, 0, i, Qt::AlignHCenter);
            }
            auto* frame = b->findChild<QFrame*>("FrameIconLayScr" + k + n);
            QLabel* icon = cc ? iconsCC_[i] : icons_[i];
            QLabel* name = cc ? labelsCC_[i] : labels_[i];
            frame->setFixedSize(96, 96);
            frame->setProperty("vclLaidOut", true);
            auto* f = new QVBoxLayout(frame);
            f->setContentsMargins(0, 0, 0, 0);
            f->addWidget(icon, 0, Qt::AlignCenter);
            icon->setFixedSize(68, 68);
            grid->addWidget(frame, 1, i, Qt::AlignHCenter);
            free(name);
            name->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
            name->setWordWrap(true);
            name->setMinimumWidth(96);
            grid->addWidget(name, 2, i);
            grid->setColumnMinimumWidth(i, 96);
        }
        grid->setColumnStretch(4, 1);
        v->addLayout(grid);
    };

    Group layouts(tr("Layouts"));
    list_->setParent(nullptr);
    list_->setMinimumSize(4 * 104 + 8, 5 * 82 + 12); // (it wraps: as many layouts across as fit)
    list_->setToolTip(QString());
    inUse_ = new QLabel;
    inUse_->setObjectName("LayoutInUse");
    inUse_->setWordWrap(true);
    QFont bold = inUse_->font();
    bold.setBold(true);
    inUse_->setFont(bold);
    include_ = new QCheckBox(tr("Include This Layout in the < > Buttons by the Pictures"));
    include_->setObjectName("IncludeLayout");
    connect(include_, &QCheckBox::toggled, this, [this](bool on) {
        if (filling_) return;
        if (!include(selected(), on)) {
            filling_ = true;
            include_->setChecked(true);
            filling_ = false;
        }
    });
    auto* all = new QPushButton(tr("Include All"));
    all->setObjectName("IncludeAllLayouts");
    all->setAutoDefault(false);
    connect(all, &QPushButton::clicked, this, [this] { includeAll(true); });
    auto* only = new QPushButton(tr("Include Only This One"));
    only->setObjectName("IncludeOnlyThisLayout");
    only->setAutoDefault(false);
    connect(only, &QPushButton::clicked, this, [this] { includeAll(false); });
    layouts.wide(inUse_)
        .wide(list_)
        .text(hint(tr("Select a layout to set it up below. OK switches the pictures to the layout selected here.")))
        .check(include_, hint(tr("The < > buttons by the pictures go through the included layouts. The ones left out "
                                 "are dimmed here.")))
        .line({all, only});

    Group panels(tr("Panels"));
    panelsBox_ = panels.box();
    ui_->ImageScrLayout->setFixedSize(ui_->ImageScrLayout->size());
    panels.wide(ui_->GroupBoxCategoryAllSystems)
        .wide(ui_->ImageScrLayout)
        .text(hint(tr("Click a panel's picture to choose what it shows. In dual and triple layouts, clear Panel 2 or "
                      "Panel 3 to leave that panel out.")));

    Group cc(tr("Consoles and Computers"));
    QGroupBox* ccBox = ui_->GroupBoxCategoryConsoleComputer;
    useConsComp_ = new QCheckBox(tr("Use Custom Categories for Consoles and Computers"));
    useConsComp_->setObjectName("UseCustomCategoryConsoleComputer");
    useConsComp_->setChecked(useCC_);
    auto sync = [this, ccBox] { ccBox->setEnabled(useConsComp_->isChecked()); };
    connect(useConsComp_, &QCheckBox::toggled, this, sync);
    sync();
    cc.check(useConsComp_, hint(tr("MAME's consoles and computers show the categories set here instead of the ones above. "
                                   "Copy takes the ones above.")))
        .wide(ccBox);
    tiles(ui_->GroupBoxCategoryAllSystems, false, {ui_->ButtonLayoutResetPanelsIndex});
    tiles(ccBox, true, {ui_->ButtonLayoutCopyCatAllSystems_ConsComp, ui_->ButtonLayoutResetPanelsIndex_ConsComp});


    settingsui::Page p("LayoutsPage", tr("Choose the layout of the game's pictures, and what each panel shows. The < > buttons "
                                         "by the pictures switch between the included layouts."), 2);
    p.add(0, layouts).add(1, panels).add(1, cc).finish();
    settingsui::setPage(this, p.widget());
    showLayout(selected());
}

int LayoutSettingsDialog::selected() const {
    for (QListView* v : findChildren<QListView*>())
        if (v->model() == model_ && v->currentIndex().isValid()) return model_->itemFromIndex(v->currentIndex())->data().toInt();
    return 0;
}

void LayoutSettingsDialog::setIcon(QLabel* icon, QLabel* label, int category, bool video) {
    const ImageCategory& c = ImageFinder::category(category);
    const QString file = video ? QStringLiteral("video_preview") : QString(c.icon);
    icon->setPixmap(QIcon(res_ + "main_icons/" + file + ".ico").pixmap(icon->size().boundedTo(QSize(64, 64))));
    icon->setAlignment(Qt::AlignCenter);
    label->setText(video ? tr("Video") : ImageFinder::title(category));
}

// LayoutListViewItemSelectionChanged: the panels of the selected layout.
void LayoutSettingsDialog::showLayout(int l) {
    filling_ = true;
    const auto& s = s_[l];
    int n = ImageLayouts::imageCount(l);
    bool quad = n == 4;
    if (panelsBox_) panelsBox_->setTitle(tr("Panels of %1").arg(layoutName(l)));
    if (include_) include_->setChecked(s.visible);
    updateInUse();
    ui_->ImageScrLayout->setPixmap(QPixmap(res_ + "images/img_layouts/" + ImageLayouts::iconFile(l) + ".png")
                                       .scaled(ui_->ImageScrLayout->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui_->ImageScrLayout->setAlignment(Qt::AlignCenter);
    // Panel 2 and 3: a check box in dual / triple layouts, a plain title in quad layouts.
    ui_->PanelEnabledScr2->setVisible(n >= 2 && !quad);
    ui_->PanelEnabledScr3->setVisible(n >= 3 && !quad);
    ui_->PanelEnabledScr2->setChecked(s.enabled[0]);
    ui_->PanelEnabledScr3->setChecked(s.enabled[1]);
    ui_->LabelPanel2->setVisible(quad);
    ui_->LabelPanel3->setVisible(quad);
    ui_->LabelPanel4->setVisible(quad);
    ui_->FrameIconLayScr2->setVisible(n >= 2);
    ui_->FrameIconLayScr3->setVisible(n >= 3);
    ui_->FrameIconLayScr4->setVisible(quad);
    ui_->LabelPanel2_ConsComp->setVisible(n >= 2);
    ui_->LabelPanel3_ConsComp->setVisible(n >= 3);
    ui_->LabelPanel4_ConsComp->setVisible(quad);
    ui_->FrameIconLayScr2_ConsComp->setVisible(n >= 2);
    ui_->FrameIconLayScr3_ConsComp->setVisible(n >= 3);
    ui_->FrameIconLayScr4_ConsComp->setVisible(quad);
    for (int i = 0; i < 4; ++i) {
        bool shown = i < n && (quad || i == 0 || i == 3 || s.enabled[i - 1]);
        icons_[i]->setVisible(shown);
        labels_[i]->setVisible(shown);
        iconsCC_[i]->setVisible(shown);
        labelsCC_[i]->setVisible(shown);
        if (shown) {
            setIcon(icons_[i], labels_[i], s.category[i], s.video[i]);
            setIcon(iconsCC_[i], labelsCC_[i], s.consComp[i], s.videoCC[i]);
        }
    }
    filling_ = false;
}

bool LayoutSettingsDialog::eventFilter(QObject* o, QEvent* e) {
    // the wheel over the layouts list scrolls the list only, also at its top or bottom (left to
    // itself, the list passes it on there, and the whole tab scrolls)
    if (e->type() == QEvent::Wheel && list_ && o == list_->viewport()) {
        QScrollBar* bar = list_->verticalScrollBar();
        const int delta = static_cast<QWheelEvent*>(e)->angleDelta().y();
        bar->setValue(bar->value() - delta * bar->singleStep() * QApplication::wheelScrollLines() / 120);
        e->accept();
        return true;
    }
    if (e->type() != QEvent::MouseButtonRelease || static_cast<QMouseEvent*>(e)->button() != Qt::LeftButton)
        return QDialog::eventFilter(o, e);
    int l = selected();
    for (int i = 0; i < 4; ++i) {
        bool cc = o == iconsCC_[i];
        if (o != icons_[i] && !cc) continue;
        int& cat = cc ? s_[l].consComp[i] : s_[l].category[i];
        bool& video = cc ? s_[l].videoCC[i] : s_[l].video[i];
        int c = pick_(video ? ImageLayouts::Video : cat);
        if (c < 0) return true;
        video = c == ImageLayouts::Video; // Video: the snapshot, the video over it
        cat = video ? 1 : c;
        if (l == 0) { // single layout: the same for both
            s_[0].category[0] = s_[0].consComp[0] = cat;
            s_[0].video[0] = s_[0].videoCC[0] = video;
        }
        showLayout(l);
        return true;
    }
    return QDialog::eventFilter(o, e);
}

// UpdateLayouts
void LayoutSettingsDialog::save() {
    WinIni ini;
    ini.load(file_);
    const bool cc = useConsComp_ ? useConsComp_->isChecked() : useCC_;
    ini.writeInteger("settings", "UseCustomCategoryConsoleComputer", cc ? 1 : 0);
    if (autoIncluded()) s_[selected()].visible = true;
    for (int l = 0; l < ImageLayouts::Count; ++l) ImageLayouts::writeCategories(ini, l, s_[l]);
    ini.save(file_);
}

} // namespace eld
