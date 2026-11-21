#include "GamesFiltersDialog.h"

#include "VclRuntime.h"
#include "WinIni.h"
#include "ui_FormArcadeGamesFilter.h"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QIcon>
#include <QKeyEvent>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QStyle>
#include <QVBoxLayout>

namespace eld {

namespace {
constexpr int kGroupRole = Qt::UserRole + 1, kHeaderRole = Qt::UserRole + 2;
// ChangeIconSize: the picture and the tile for each size.
const QSize kIcon[] = {{128, 128}, {68, 68}, {48, 48}, {30, 24}};
const QSize kTile[] = {{300, 138}, {214, 78}, {194, 58}, {184, 34}};
const int kHeight[] = {760, 600, 516, 516};
} // namespace

// MountFiltersList: [main] filter=description; a filter with a section of its own is a group
// ("List All" first, then the section's filters).
bool GamesFiltersDialog::readFilters(const QString& iniFile, QList<Filter>& out, QString* error) {
    out.clear();
    WinIni ini;
    if (!QFileInfo::exists(iniFile) || !ini.load(iniFile)) {
        if (error) *error = tr("File %1 was not found. The filters list cannot be loaded.").arg(QDir::toNativeSeparators(iniFile));
        return false;
    }
    const QStringList main = ini.keys("main");
    if (main.isEmpty()) {
        if (error) *error = tr("The [main] section of %1 is empty or missing. It's needed for the filters to work.")
                                .arg(QDir::toNativeSeparators(iniFile));
        return false;
    }
    for (const QString& name : main) {
        QString desc = ini.readString("main", name).trimmed();
        if (ini.sectionExists(name)) {
            out << Filter{name, tr("List All"), desc};
            for (const QString& sub : ini.keys(name)) out << Filter{sub, ini.readString(name, sub).trimmed(), desc};
        } else {
            out << Filter{name, desc, QString()};
        }
    }
    return true;
}

GamesFiltersDialog::GamesFiltersDialog(const QList<Filter>& filters, const QString& current, const QString& iconFolder,
                                       int iconSize, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormArcadeGamesFilter), filters_(filters), current_(current), iconFolder_(iconFolder),
      size_(std::clamp(iconSize, 0, 3)) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("Games Filters - MAME and Arcade"));
    list_ = new QListWidget(this);
    list_->setViewMode(QListView::ListMode);
    list_->setFlow(QListView::LeftToRight);
    list_->setWrapping(true);
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    list_->installEventFilter(this);
    ui_->LabelToolBarIconSize->setText(tr("View Icon Size"));
    ui_->ButtonGoToCurrentFilter->setText(tr("Go to Current"));

    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        if (it->data(kHeaderRole).toBool()) toggleGroup(it);
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        if (!it->data(kHeaderRole).toBool()) accept();
    });
    connect(list_, &QListWidget::currentItemChanged, this, [this] {
        ui_->ButtonGoToCurrentFilter->setEnabled(selected() != current_);
    });
    const QPair<QPushButton*, int> sizes[] = {{ui_->IconSize4KUltraHD, 0}, {ui_->IconSizeExtraLarge, 1},
                                              {ui_->IconSizeLarge, 2}, {ui_->IconSizeSmall, 3}};
    for (const auto& size : sizes) {
        QPushButton* b = size.first;
        const int s = size.second;
        b->setCheckable(true);
        b->setAutoExclusive(true);
        connect(b, &QPushButton::clicked, this, [this, s] { setIconSize(s); });
        if (s == size_) b->setChecked(true);
    }
    connect(ui_->ButtonGoToCurrentFilter, &QPushButton::clicked, this, &GamesFiltersDialog::goToCurrent);
    connect(ui_->ButtonOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui_->ButtonCancel, &QPushButton::clicked, this, &QDialog::reject);
    for (QPushButton* b : findChildren<QPushButton*>()) b->setAutoDefault(false);
    // the list over the bottom bar (its controls laid out from the design: every text whole)
    vcl::autoLayout(ui_->PanelBottom);
    ui_->PanelBottom->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* root = vcl::boxed(new QVBoxLayout(this), 0, 0, 0, 0, 0);
    root->addWidget(list_, 1);
    root->addWidget(ui_->PanelBottom);
    setProperty("vclLaidOut", true);
    fill();
    setIconSize(size_);
    goToCurrent();
    list_->setFocus();
}

GamesFiltersDialog::~GamesFiltersDialog() = default;

void GamesFiltersDialog::fill() {
    list_->clear();
    QFont bold = list_->font();
    bold.setBold(true);
    QString group = "\x01";
    auto header = [&](const QString& title) {
        auto* h = new QListWidgetItem(QStringLiteral("▾  ") + title, list_);
        h->setData(kHeaderRole, true);
        h->setData(kGroupRole, title);
        h->setFlags(Qt::ItemIsEnabled);
        h->setFont(bold);
        h->setForeground(palette().color(QPalette::Highlight));
    };
    // General first (the filters without a section), then the groups in [main]'s order.
    header(tr("General"));
    for (const Filter& f : filters_)
        if (f.group.isEmpty()) {
            auto* it = new QListWidgetItem(f.description, list_);
            it->setData(Qt::UserRole, f.name);
            it->setData(kGroupRole, tr("General"));
            it->setToolTip(f.description);
            if (f.name == "allgames") it->setFont(bold);
        }
    for (const Filter& f : filters_) {
        if (f.group.isEmpty()) continue;
        if (f.group != group) {
            group = f.group;
            header(group);
        }
        auto* it = new QListWidgetItem(f.description, list_);
        it->setData(Qt::UserRole, f.name);
        it->setData(kGroupRole, group);
        it->setToolTip(f.description == tr("List All") ? group + " - " + f.description : f.description);
    }
}

void GamesFiltersDialog::setIconSize(int size) {
    size_ = std::clamp(size, 0, 3);
    static const char* names[] = {QT_TR_NOOP("> 4K UltraHD (128x128)"), QT_TR_NOOP("> Extra Large (68x68)"),
                                  QT_TR_NOOP("> Large (48x48)"), QT_TR_NOOP("> Small (30x24)")};
    ui_->LabelIconSizeValue->setText(tr(names[size_]));
    list_->setIconSize(kIcon[size_]);
    QFont f = list_->font();
    f.setPointSize(size_ == 3 ? 8 : 9);
    list_->setFont(f);
    // Three tiles side by side, as the original: the window is as wide as they are.
    const QSize tile = kTile[size_];
    int sb = list_->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    int w = tile.width() * 3 + sb + 2 * list_->frameWidth() + 2;
    // three tiles across (or the bottom bar's width, when its texts need more); the list's height
    // as designed for the size, with the bottom bar as high as it needs
    const int bar = ui_->PanelBottom->sizeHint().height();
    w = std::max(w, ui_->PanelBottom->minimumSizeHint().width());
    setFixedSize(w, kHeight[size_] - 49 + bar);
    int inner = w - sb - 2 * list_->frameWidth(); // the viewport, before it's laid out
    QIcon none(QDir(iconFolder_).filePath("no_icon.ico"));
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* it = list_->item(i);
        if (it->data(kHeaderRole).toBool()) {
            it->setSizeHint(QSize(inner - 2, 28)); // a whole row: the tiles start on the next one
            continue;
        }
        it->setSizeHint(tile);
        QString file = QDir(iconFolder_).filePath(it->data(Qt::UserRole).toString() + ".ico");
        it->setIcon(QFileInfo::exists(file) ? QIcon(file) : none);
    }
    list_->doItemsLayout();
    if (QListWidgetItem* cur = list_->currentItem()) list_->scrollToItem(cur, QAbstractItemView::EnsureVisible);
}

void GamesFiltersDialog::toggleGroup(QListWidgetItem* header) {
    const QString group = header->data(kGroupRole).toString();
    bool fold = header->text().startsWith(QStringLiteral("▾"));
    header->setText((fold ? QStringLiteral("▸  ") : QStringLiteral("▾  ")) + group);
    for (int i = list_->row(header) + 1; i < list_->count(); ++i) {
        QListWidgetItem* it = list_->item(i);
        if (it->data(kHeaderRole).toBool()) break;
        it->setHidden(fold);
    }
}

// ButtonGoToCurrentFilterClick: the filter in use, its group unfolded.
void GamesFiltersDialog::goToCurrent() {
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* it = list_->item(i);
        if (it->data(kHeaderRole).toBool() || it->data(Qt::UserRole).toString() != current_) continue;
        if (it->isHidden())
            for (int h = i - 1; h >= 0; --h)
                if (list_->item(h)->data(kHeaderRole).toBool()) { toggleGroup(list_->item(h)); break; }
        list_->setCurrentItem(it);
        list_->scrollToItem(it, QAbstractItemView::PositionAtCenter);
        break;
    }
    ui_->ButtonGoToCurrentFilter->setEnabled(false);
    list_->setFocus();
}

QString GamesFiltersDialog::selected() const {
    QListWidgetItem* it = list_->currentItem();
    return it && !it->data(kHeaderRole).toBool() && !it->isHidden() ? it->data(Qt::UserRole).toString() : QString();
}

bool GamesFiltersDialog::eventFilter(QObject* o, QEvent* e) {
    if (o == list_ && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
            QListWidgetItem* it = list_->currentItem();
            if (it && it->data(kHeaderRole).toBool()) toggleGroup(it);
            else accept();
            return true;
        }
    }
    return QDialog::eventFilter(o, e);
}

} // namespace eld
