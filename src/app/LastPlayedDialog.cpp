#include "LastPlayedDialog.h"

#include "VclRuntime.h"
#include "ui_FormLastPlayedGames.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

namespace eld {

namespace {
// The original's columns (the header labels sit over them): Title, Game Name, Software Title,
// Last Played, Total Playtime.
const int kColumnWidths[] = {500, 139, 170, 180, 189}; // the original: 579, 139, 180, 124 (a smaller font)
const int kRowHeight = 22;    // 25 rows + 14 = the list's 564 pixels
const int kSystemsHeight = 92; // one row of system icons (the original has room for 4 rows of 19)
} // namespace

LastPlayedDialog::LastPlayedDialog(const QList<System>& systems, SystemId current,
                                   std::function<bool(SystemId, const QString&)> select, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormLastPlayedGames), systems_(systems), select_(std::move(select)) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("Last Played Games [Top %1]").arg(kTop));

    // Systems: an icon per system with play statistics (the original's EasyListview in icon view).
    systemsView_ = new QListWidget(this);
    systemsView_->setViewMode(QListView::IconMode);
    systemsView_->setIconSize(QSize(48, 48));
    systemsView_->setGridSize(QSize(96, 82));
    systemsView_->setMovement(QListView::Static);
    systemsView_->setResizeMode(QListView::Adjust);
    systemsView_->setFrameShape(QFrame::NoFrame);
    systemsView_->setFlow(QListView::LeftToRight);
    systemsView_->setWrapping(true);
    systemsView_->setFont(QFont("Verdana", 8));
    int start = 0;
    for (int i = 0; i < systems_.size(); ++i) {
        auto* it = new QListWidgetItem(systems_[i].icon, systems_[i].title, systemsView_);
        it->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        if (systems_[i].id == current) start = i;
    }

    // Games list: the header is the row of labels over it (PanelPlayedListHeader).
    QTreeView* list = ui_->LastPlayedList;
    model_ = new QStandardItemModel(0, 5, this);
    list->setModel(model_);
    list->setHeaderHidden(true);
    list->setRootIsDecorated(false);
    list->setUniformRowHeights(true);
    list->setAlternatingRowColors(false);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list->setSelectionBehavior(QAbstractItemView::SelectRows);
    list->setIconSize(QSize(36, 16));
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setStyleSheet(QString("QTreeView::item { height: %1px; }").arg(kRowHeight));
    list->header()->setStretchLastSection(true);
    connect(list, &QTreeView::doubleClicked, this, [this] { selectGame(false); });
    ui_->LabelTitleCaption->setText(tr("Title"));
    ui_->LabelGameNameCaption->setText(tr("Game Name"));
    ui_->LabelSoftwareTitleCaption->setText(tr("Software Title"));
    ui_->LabelLastPlayed->setText(tr("Last Played"));
    ui_->LabelTotalPlaytime->setText(tr("Total Playtime"));

    ui_->ButtonSelectGame->setText(tr("Select Game"));
    ui_->ButtonSelectGameExit->setText(tr("Select Game and Exit"));
    ui_->ButtonClose->setText(tr("Close"));
    ui_->ButtonSelectGame->setToolTip(tr("Select current game in main games list"));
    ui_->ButtonSelectGameExit->setToolTip(tr("Select current game in main games list and close this dialog"));
    ui_->ButtonClose->setToolTip(tr("Close this dialog"));
    connect(ui_->ButtonSelectGame, &QPushButton::clicked, this, [this] { selectGame(false); });
    connect(ui_->ButtonSelectGameExit, &QPushButton::clicked, this, [this] { selectGame(true); });
    connect(ui_->ButtonClose, &QPushButton::clicked, this, &QDialog::reject);
    ui_->ButtonSelectGame->setDefault(false);
    ui_->ButtonSelectGame->setAutoDefault(false);
    ui_->ButtonSelectGameExit->setAutoDefault(false);
    ui_->ButtonClose->setAutoDefault(false);

    layoutWindow();
    connect(systemsView_, &QListWidget::currentRowChanged, this, &LastPlayedDialog::showSystem);
    systemsView_->setCurrentRow(start);
    showSystem(start);
    list->installEventFilter(this);
    list->setFocus();
}

LastPlayedDialog::~LastPlayedDialog() = default;

// SystemsItemSelectionChanged + FilterMRUGames: the chosen system's games.
void LastPlayedDialog::showSystem(int row) {
    if (row < 0 || row >= systems_.size() || row == shown_) return;
    shown_ = row;
    const System& s = systems_[row];
    ui_->LabelSystemTitle->setText(s.title.toUpper());
    ui_->LabelSystemType->setText(tr("ARCADE"));
    model_->removeRows(0, model_->rowCount());
    // The favorite star (StateImageIndex) before the game's icon; an empty space otherwise.
    auto withStar = [&](const Game& g) {
        QPixmap pm(36, 16);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        if (g.favorite) s.star.paint(&p, QRect(0, 0, 16, 16));
        g.icon.paint(&p, QRect(20, 0, 16, 16));
        return QIcon(pm);
    };
    for (const Game& g : s.games) {
        QList<QStandardItem*> row;
        auto* title = new QStandardItem(withStar(g), g.title);
        title->setData(g.name, Qt::UserRole);
        title->setToolTip(g.title);
        row << title << new QStandardItem(g.name.section('\\', -1)) << new QStandardItem(g.softwareTitle)
            << new QStandardItem(g.lastPlayed)
            << new QStandardItem(g.playtime.isEmpty() ? QString("(%1x)").arg(g.played)
                                                      : QString("%1 (%2x)").arg(g.playtime).arg(g.played));
        model_->appendRow(row);
    }
    if (model_->rowCount()) ui_->LastPlayedList->setCurrentIndex(model_->index(0, 0));
}

// LastPlayedListKeyAction / FormKeyPress: Enter selects the game, Escape closes.
bool LastPlayedDialog::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::KeyPress) {
        int k = static_cast<QKeyEvent*>(e)->key();
        if (k == Qt::Key_Return || k == Qt::Key_Enter) {
            selectGame(false);
            return true;
        }
    }
    return QDialog::eventFilter(o, e);
}

// SelectGameToPlay
void LastPlayedDialog::selectGame(bool close) {
    QModelIndex i = ui_->LastPlayedList->currentIndex();
    if (!i.isValid() || shown_ < 0) return;
    QString name = model_->item(i.row(), 0)->data(Qt::UserRole).toString();
    if (!select_ || !select_(systems_[shown_].id, name)) {
        QMessageBox::information(this, tr("Last Played Games"),
                                 tr("This game couldn't be selected in the games list: the filters or the search hide it, or "
                                    "it's no longer in the list."));
        return;
    }
    if (close) accept();
}

// In layouts: the systems on top, the system's title, the games under their column titles (each
// column at least as wide as its title, in any language), the buttons centered at the bottom.
void LastPlayedDialog::layoutWindow() {
    using vcl::boxed, vcl::loose;
    systemsView_->setFixedHeight(kSystemsHeight);
    // the system's title centered, ARCADE at the left
    QWidget* title = ui_->PanelSystemsTitle;
    auto* t = new QGridLayout(title);
    t->setContentsMargins(32, 0, 8, 0);
    for (QLabel* l : {(QLabel*)ui_->LabelSystemType, (QLabel*)ui_->LabelSystemTitle}) loose(l);
    ui_->LabelSystemTitle->setAlignment(Qt::AlignCenter);
    t->addWidget(ui_->LabelSystemType, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    t->addWidget(ui_->LabelSystemTitle, 0, 1);
    t->setColumnStretch(0, 1);
    t->setColumnStretch(2, 1);
    // the column titles over the list's columns
    QWidget* header = ui_->PanelPlayedListHeader;
    auto* h = boxed(new QHBoxLayout(header), 0, 3, 0, 3, 0);
    QLabel* labels[] = {ui_->LabelTitleCaption, ui_->LabelGameNameCaption, ui_->LabelSoftwareTitleCaption,
                        ui_->LabelLastPlayed, ui_->LabelTotalPlaytime};
    int total = 0;
    for (int c = 0; c < 5; ++c) {
        QLabel* l = labels[c];
        loose(l);
        const int indent = c == 0 ? 14 : 4; // over the column's text
        const int w = std::max(kColumnWidths[c], indent + l->fontMetrics().horizontalAdvance(l->text()) + 10);
        ui_->LastPlayedList->setColumnWidth(c, w);
        h->addSpacing(indent);
        l->setFixedWidth(w - indent);
        h->addWidget(l);
        total += w;
    }
    h->addStretch();
    QWidget* games = ui_->PanelGames;
    auto* g = boxed(new QVBoxLayout(games), 0, 0, 0, 0, 0);
    g->addWidget(header);
    g->addWidget(ui_->LastPlayedList);
    ui_->LastPlayedList->setFixedHeight(kRowHeight * kTop + 14);
    // Select Game, Select Game and Exit, Close: centered
    QWidget* bottom = ui_->PanelBottom;
    auto* b = boxed(new QHBoxLayout(bottom), 8, 8, 8, 8, 6);
    b->addStretch();
    for (QPushButton* x : {ui_->ButtonSelectGame, ui_->ButtonSelectGameExit, ui_->ButtonClose}) b->addWidget(vcl::textButton(x));
    b->addStretch();
    auto* root = boxed(new QVBoxLayout(this), 0, 0, 0, 0, 0);
    root->addWidget(systemsView_);
    root->addWidget(title);
    root->addWidget(games);
    root->addWidget(bottom);
    root->addStrut(std::max(1178, total)); // (the columns' width: the window as wide)
    root->setSizeConstraint(QLayout::SetFixedSize);
    for (QWidget* x : {(QWidget*)this, title, header, games, bottom}) x->setProperty("vclLaidOut", true);
}

} // namespace eld
