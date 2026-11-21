#include "MultiSlotDialog.h"

#include "ElWidgets.h"
#include "VclRuntime.h"
#include "ui_FormArcadeMultiSlotGames.h"

#include <QHeaderView>
#include <QKeyEvent>
#include <QLocale>
#include <QStandardItemModel>
#include <QTreeView>

namespace eld {

namespace {
enum Role { NameRole = Qt::UserRole + 1, StatusRole };

QString slotText(int n) { // "6-slot" ("1-slot" apart: the languages that say it differently)
    return n == 1 ? MultiSlotDialog::tr("1-slot") : MultiSlotDialog::tr("%1-slot").arg(n);
}

void setupList(QTreeView* v) {
    v->setRootIsDecorated(false);
    v->setUniformRowHeights(true);
    v->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->setSelectionBehavior(QAbstractItemView::SelectRows);
    v->setItemsExpandable(false);
    v->header()->setSectionsMovable(false);
    v->header()->setStretchLastSection(true);
}
} // namespace

MultiSlotDialog::MultiSlotDialog(const Input& in, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormArcadeMultiSlotGames), in_(in) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(MultiSlot::title(in_.machine));
    const bool neoGeo = in_.machine == QLatin1String("neogeo") && !in_.boards.isEmpty();

    ui_->LabelHelpText->setText(tr("Choose a cartridge slot for each game (%1 slots max)").arg(MultiSlot::maxSlots(in_.machine)));
    ui_->ButtonOk->setText(tr("OK"));
    ui_->ButtonNo->setText(tr("Cancel"));
    ui_->ButtonUp->setText(tr("Move Up"));
    ui_->ButtonUp->setToolTip(tr("Move selected game to upper slot [Ctrl+Up Arrow]"));
    ui_->ButtonDown->setText(tr("Move Down"));
    ui_->ButtonDown->setToolTip(tr("Move selected game to lower slot [Ctrl+Down Arrow]"));
    ui_->ButtonRemoveFromList->setText(tr("Remove"));
    ui_->ButtonRemoveFromList->setToolTip(tr("Remove selected games from the list [Delete]"));
    ui_->ButtonChangePanelNeoGeoMVS->setText(tr("Change"));
    ui_->ButtonChangePanelNeoGeoMVS->setToolTip(tr("Select another machine to play games with"));
    ui_->ButtonChangePanelNeoGeoMVS->show(); // (the form hides it: there was no other way to the boards)
    ui_->LabelMultiSlotMachinesChooseMachineToRun->setText(tr("Choose machine to run games with, click \"OK\" button"));
    ui_->LabelMultiSlotMachinesBoldDefaultMachine->setText(tr("default machine is in bold"));
    ui_->ButtonOkMultiSlotMachines->setText(tr("OK"));
    ui_->ButtonCancelMultiSlotMachines->setText(tr("Cancel"));
    ui_->MultiSlotMachines->setToolTip(tr("Select another machine to play games with (mouse double-click or ENTER key)"));
    // The games, one per slot
    games_ = new QStandardItemModel(0, 7, this);
    games_->setHorizontalHeaderLabels({tr("Slot"), tr("Title"), tr("Name"), tr("Clone of"), tr("Played"), tr("Players"), tr("Status")});
    const char* status[] = {QT_TR_NOOP("Good"), QT_TR_NOOP("Imperfect"), QT_TR_NOOP("Preliminary")};
    const QLocale loc;
    bool clones = false;
    for (const MameGame& g : in_.games) {
        QList<QStandardItem*> row{new QStandardItem(), new QStandardItem(in_.icon ? in_.icon(g) : QIcon(), g.title), new QStandardItem(g.name),
                                  new QStandardItem(g.cloneOf), new QStandardItem(g.played ? loc.toString(g.played) + "x" : QString()),
                                  new QStandardItem(in_.players ? in_.players(g) : QString()),
                                  new QStandardItem(g.driverStatus >= 0 && g.driverStatus <= 2 ? tr(status[g.driverStatus]) : QString())};
        row[0]->setData(g.name, NameRole);
        row[0]->setData(int(g.setStatus), StatusRole);
        QFont f = font();
        f.setBold(true);
        row[0]->setFont(f);
        row[0]->setTextAlignment(Qt::AlignCenter);
        clones |= !g.cloneOf.isEmpty();
        games_->appendRow(row);
    }
    QTreeView* v = ui_->GamesList;
    setupList(v);
    v->setModel(games_);
    v->setSelectionMode(QAbstractItemView::ExtendedSelection);
    v->setIconSize(QSize(16, 16));
    const int widths[] = {44, 420, 110, 100, 70, 90};
    for (int c = 0; c < 6; ++c) v->setColumnWidth(c, widths[c]);
    // the Slot column as wide as its (translated) header
    v->setColumnWidth(0, std::max(widths[0], v->header()->fontMetrics().horizontalAdvance(games_->headerData(0, Qt::Horizontal).toString()) + 20));
    v->setColumnHidden(3, !clones);
    v->installEventFilter(this);
    connect(v->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        const int n = int(ui_->GamesList->selectionModel()->selectedRows().size());
        ui_->ButtonUp->setEnabled(n == 1);
        ui_->ButtonDown->setEnabled(n == 1);
        ui_->ButtonRemoveFromList->setEnabled(n > 0);
    });

    // The Neo-Geo boards
    boards_ = new QStandardItemModel(0, 6, this);
    boards_->setHorizontalHeaderLabels({tr("Machine"), tr("Year"), tr("Slots"), tr("Video Chipset"), tr("Name"), tr("Notes")});
    for (int i = 0; i < in_.boards.size(); ++i) {
        const MultiSlot::Board& b = in_.boards[i];
        QList<QStandardItem*> row{new QStandardItem(b.title), new QStandardItem(b.year), new QStandardItem(slotText(b.slotCount)),
                                  new QStandardItem(b.chipset), new QStandardItem(b.name), new QStandardItem(b.notes)};
        row[0]->setData(b.name, NameRole);
        row[5]->setToolTip(b.notes);
        if (b.name == QLatin1String("neogeo")) { // the default one
            QFont f = font();
            f.setBold(true);
            for (QStandardItem* it : row) it->setFont(f);
        }
        boards_->appendRow(row);
    }
    QTreeView* m = ui_->MultiSlotMachines;
    setupList(m);
    m->setModel(boards_);
    m->setSelectionMode(QAbstractItemView::SingleSelection);
    const int bw[] = {170, 50, 60, 170, 80};
    for (int c = 0; c < 5; ++c) m->setColumnWidth(c, bw[c]);
    for (int c : {1, 2}) // Year, Slots: their (translated) texts whole
        if (const int w = static_cast<QAbstractItemView*>(m)->sizeHintForColumn(c) + 12; w > m->columnWidth(c)) m->setColumnWidth(c, w);
    m->installEventFilter(this);
    connect(m, &QTreeView::doubleClicked, this, [this] { hideBoards(true); });
    connect(ui_->ButtonChangePanelNeoGeoMVS, &QPushButton::clicked, this, &MultiSlotDialog::showBoards);
    connect(ui_->ButtonOkMultiSlotMachines, &QPushButton::clicked, this, [this] { hideBoards(true); });
    connect(ui_->ButtonCancelMultiSlotMachines, &QPushButton::clicked, this, [this] { hideBoards(false); });
    ui_->PanelMultiSlotMachines->hide();
    vcl::setOverlay(ui_->PanelMultiSlotMachines); // (it opens over the games list)

    connect(ui_->ButtonUp, &QPushButton::clicked, this, [this] { moveSelected(true); });
    connect(ui_->ButtonDown, &QPushButton::clicked, this, [this] { moveSelected(false); });
    connect(ui_->ButtonRemoveFromList, &QPushButton::clicked, this, &MultiSlotDialog::removeSelected);
    connect(ui_->ButtonOk, &QPushButton::clicked, this, [this] { if (!games().isEmpty()) accept(); });
    connect(ui_->ButtonNo, &QPushButton::clicked, this, &QDialog::reject);
    for (QPushButton* b : findChildren<QPushButton*>()) b->setAutoDefault(false);
    ui_->ButtonOk->setDefault(true);

    // FormShow: the board's panel only for the Neo-Geo
    ui_->PanelNeoGeoMVS->setVisible(neoGeo);
    vcl::layoutForm(this); // every text whole in any language and size
    board_.clear();
    if (neoGeo) setBoard(in_.board);
    updateSlots();
    if (games_->rowCount()) selectRow(0);
    v->setFocus();
}

MultiSlotDialog::~MultiSlotDialog() = default;

int MultiSlotDialog::slotCount() const {
    for (const MultiSlot::Board& b : in_.boards)
        if (b.name == board_) return b.slotCount;
    return MultiSlot::maxSlots(in_.machine);
}

QStringList MultiSlotDialog::listed() const {
    QStringList out;
    for (int r = 0; r < games_->rowCount(); ++r) out << games_->item(r, 0)->data(NameRole).toString();
    return out;
}

QStringList MultiSlotDialog::games() const { return listed().mid(0, slotCount()); }

bool MultiSlotDialog::boardsShown() const { return ui_->PanelMultiSlotMachines->isVisibleTo(this); }

// UpdateSlotIndex: the slot numbers; the games past the board's slots grayed (left out)
void MultiSlotDialog::updateSlots() {
    const int count = slotCount();
    for (int r = 0; r < games_->rowCount(); ++r) {
        games_->item(r, 0)->setText(QString::number(r + 1));
        const bool out = r >= count;
        const bool missing = games_->item(r, 0)->data(StatusRole).toInt() == 1;
        for (int c = 0; c < games_->columnCount(); ++c)
            games_->item(r, c)->setForeground(out ? QColor(0x80, 0x80, 0x80) : missing ? QColor(0xC0, 0, 0) : QColor(0x40, 0x40, 0x40));
        games_->item(r, 0)->setToolTip(out ? tr("This machine has %1 slots: this game stays out").arg(count) : QString());
    }
    ui_->ButtonOk->setEnabled(games_->rowCount() > 0 && !boardsShown());
    const int n = int(ui_->GamesList->selectionModel() ? ui_->GamesList->selectionModel()->selectedRows().size() : 0);
    ui_->ButtonUp->setEnabled(n == 1);
    ui_->ButtonDown->setEnabled(n == 1);
    ui_->ButtonRemoveFromList->setEnabled(n > 0);
}

void MultiSlotDialog::selectRow(int row) {
    if (row < 0 || row >= games_->rowCount()) return;
    QTreeView* v = ui_->GamesList;
    v->selectionModel()->setCurrentIndex(games_->index(row, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

// MoveToSlot
void MultiSlotDialog::moveSelected(bool up) {
    const auto rows = ui_->GamesList->selectionModel()->selectedRows();
    if (rows.size() != 1) return;
    const int r = rows.first().row(), to = up ? r - 1 : r + 1;
    if (to < 0 || to >= games_->rowCount()) return;
    games_->insertRow(to, games_->takeRow(r));
    updateSlots();
    selectRow(to);
    ui_->GamesList->setFocus();
}

void MultiSlotDialog::removeSelected() {
    QList<int> rows;
    for (const QModelIndex& i : ui_->GamesList->selectionModel()->selectedRows()) rows << i.row();
    if (rows.isEmpty()) return;
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int r : rows) games_->removeRow(r);
    updateSlots();
    selectRow(std::min(rows.last(), games_->rowCount() - 1));
    ui_->GamesList->setFocus();
}

// UpdateLabelSelectedMachine: "Use Machine: Neo-Geo MV-6F    1990    6-slot    [neogeo]"
void MultiSlotDialog::setBoard(const QString& name) {
    int pick = 0;
    for (int i = 0; i < in_.boards.size(); ++i)
        if (in_.boards[i].name == name) pick = i;
    if (in_.boards.isEmpty()) return;
    const MultiSlot::Board& b = in_.boards[pick];
    board_ = b.name;
    ui_->LabelMultiSlotMachines->setText(tr("Use Machine: %1").arg(b.title) + "    " + b.year + "    " + slotText(b.slotCount) +
                                         "    [" + b.name + "]");
    ui_->MultiSlotMachines->setCurrentIndex(boards_->index(pick, 0));
    updateSlots();
}

void MultiSlotDialog::showBoards() {
    if (in_.boards.isEmpty()) return;
    ui_->PanelMultiSlotMachines->show();
    ui_->PanelMultiSlotMachines->raise();
    setBoard(board_); // the board in use selected
    ui_->ButtonOk->setEnabled(false);
    ui_->MultiSlotMachines->setFocus();
}

void MultiSlotDialog::chooseBoard(const QString& name) {
    for (int i = 0; i < boards_->rowCount(); ++i)
        if (boards_->item(i, 0)->data(NameRole).toString() == name) ui_->MultiSlotMachines->setCurrentIndex(boards_->index(i, 0));
    hideBoards(true);
}

void MultiSlotDialog::hideBoards(bool use) {
    const QModelIndex i = ui_->MultiSlotMachines->currentIndex();
    ui_->PanelMultiSlotMachines->hide();
    if (use && i.isValid()) setBoard(boards_->item(i.row(), 0)->data(NameRole).toString());
    updateSlots();
    ui_->GamesList->setFocus();
}

// Ctrl+Up / Ctrl+Down: move; Delete: remove; Enter in the boards: OK; Escape there: Cancel
bool MultiSlotDialog::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (o == ui_->GamesList) {
            if (k->modifiers() == Qt::ControlModifier && (k->key() == Qt::Key_Up || k->key() == Qt::Key_Down)) {
                moveSelected(k->key() == Qt::Key_Up);
                return true;
            }
            if (k->key() == Qt::Key_Delete) {
                removeSelected();
                return true;
            }
        } else if (o == ui_->MultiSlotMachines) {
            if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
                hideBoards(true);
                return true;
            }
            if (k->key() == Qt::Key_Escape) {
                hideBoards(false);
                return true;
            }
        }
    }
    return QDialog::eventFilter(o, e);
}

} // namespace eld
