#include "SelectMachineDialog.h"

#include "ElWidgets.h"
#include "VclRuntime.h"
#include "WinIni.h"
#include "ui_FormArcadeSoftwareListMachineToRunGame.h"

#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTreeView>

namespace eld {

namespace {
const char* kSection = "SelectMachineSoftwareListGame";
enum Role { NameRole = Qt::UserRole + 1, StatusRole, DriverRole };
} // namespace

// ChangeFilters: the machine in use and the selected one always stay.
class MachinesFilter : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    bool availableOnly = false, hidePreliminary = false;
    QString keep1, keep2;
    void refresh() { invalidateFilter(); }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        const QModelIndex i = sourceModel()->index(row, 0, parent);
        const QString name = i.data(NameRole).toString();
        if (name == keep1 || name == keep2) return true;
        if (availableOnly && i.data(StatusRole).toInt() != 0) return false;
        if (hidePreliminary && i.data(DriverRole).toInt() == 2) return false;
        return true;
    }
};

SelectMachineDialog::SelectMachineDialog(const Input& in, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormArcadeSoftwareListMachineToRunGame), in_(in) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("Select a Machine to Run the Game With"));
    const MameGame& g = in_.game;

    // Top bar: the game, its list and how to use it.
    static const int topColors[] = {0xF0FAE5, 0xE5F0FA, 0xEEEEEE};
    ui_->TopBar->setProperty("vclColor1", ElColorBox::toQColor(topColors[std::clamp<int>(g.setStatus, 0, 2)]).name());
    ui_->TopBar->setProperty("vclColor2", QColor(0xF1F1F1).name());
    ui_->GameIcon->setPixmap(in_.gameIcon.pixmap(68, 68));
    ui_->GameIcon->setAlignment(Qt::AlignCenter);
    ui_->LabelGameTitle->setText(g.title);
    QString name = tr("name: %1").arg(in_.nameLine);
    if (!g.usage.isEmpty()) name += "\n" + tr("usage: %1").arg(g.usage);
    ui_->LabelGameName->setText(name);
    ui_->LabelSoftwarelistTitleW->setText(g.softwareTitle);

    ui_->ButtonYes->setText(tr("OK"));
    ui_->ButtonNo->setText(tr("Cancel"));
    ui_->ButtonResetToCurrent->setText(tr("Reset"));
    ui_->ButtonResetToCurrent->setToolTip(tr("Reset selection to current machine"));
    ui_->ShowAvailableMachinesOnly->setText(tr("Show Available Only"));
    ui_->ShowAvailableMachinesOnly->setToolTip(tr("Check to hide all machines you don't have ROMs for"));
    ui_->HidePreliminaryMachines->setText(tr("Hide Preliminary Machines"));
    ui_->HidePreliminaryMachines->setToolTip(tr("Hide machines with drivers tagged as preliminary"));
    // The machines
    model_ = new QStandardItemModel(0, 6, this);
    model_->setHorizontalHeaderLabels({tr("Machine"), tr("Name"), tr("Manufacturer"), tr("Hz"), tr("Clone of"), tr("Save State")});
    QFont bold = font();
    bold.setBold(true);
    for (const MameGame& m : in_.machines) {
        QString hz;
        bool ok = false;
        double v = m.refresh.toDouble(&ok);
        if (ok) hz = QString::number(v, 'f', 2) + " Hz";
        const QString ss = m.saveState == 1 ? tr("Supported") : m.saveState == 0 ? tr("Unsupported") : QString();
        QList<QStandardItem*> row{new QStandardItem(in_.icon ? in_.icon(m) : QIcon(), m.title), new QStandardItem(m.name),
                                  new QStandardItem(m.manufacturer), new QStandardItem(hz),
                                  new QStandardItem(m.cloneOf), new QStandardItem(ss)};
        row[0]->setData(m.name, NameRole);
        row[0]->setData(int(m.setStatus), StatusRole);
        row[0]->setData(int(m.driverStatus), DriverRole);
        for (QStandardItem* it : row) {
            if (m.name == in_.current) it->setFont(bold); // the machine in use
            if (m.setStatus == 2) it->setForeground(QColor(0x80, 0x80, 0x80));
        }
        model_->appendRow(row);
    }
    filter_ = new MachinesFilter(this);
    filter_->setSourceModel(model_);
    filter_->setSortCaseSensitivity(Qt::CaseInsensitive);
    filter_->keep1 = in_.current;
    QTreeView* v = ui_->MachinesListView;
    v->setModel(filter_);
    v->setRootIsDecorated(false);
    v->setUniformRowHeights(true);
    v->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->setSelectionBehavior(QAbstractItemView::SelectRows);
    v->setSelectionMode(QAbstractItemView::SingleSelection);
    v->setIconSize(QSize(24, 24));
    v->setSortingEnabled(true);
    v->sortByColumn(0, Qt::AscendingOrder);
    const int widths[] = {390, 110, 160, 90, 90}; // the rest for Save State, with no scroll bar
    for (int c = 0; c < 5; ++c) v->setColumnWidth(c, widths[c]);
    connect(v, &QTreeView::doubleClicked, this, [this] { if (!machine().isEmpty()) accept(); });
    connect(v->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { filter_->keep2 = machine(); });

    WinIni ini;
    ini.load(in_.extrasIni);
    ui_->ShowAvailableMachinesOnly->setChecked(ini.readInteger(kSection, "ShowAvailableMachinesOnly", 0) != 0);
    ui_->HidePreliminaryMachines->setChecked(ini.readInteger(kSection, "HidePreliminaryMachines", 0) != 0);
    connect(ui_->ShowAvailableMachinesOnly, &QCheckBox::toggled, this, [this] { applyFilters(); });
    connect(ui_->HidePreliminaryMachines, &QCheckBox::toggled, this, [this] { applyFilters(); });
    connect(ui_->ButtonResetToCurrent, &QPushButton::clicked, this, [this] { selectMachine(in_.current); });
    connect(ui_->ButtonYes, &QPushButton::clicked, this, [this] { if (!machine().isEmpty()) accept(); });
    connect(ui_->ButtonNo, &QPushButton::clicked, this, &QDialog::reject);
    for (QPushButton* b : findChildren<QPushButton*>()) b->setAutoDefault(false);
    ui_->ButtonYes->setDefault(true);
    vcl::layoutForm(this); // every text whole in any language and size
    applyFilters();
    selectMachine(in_.current);
}

SelectMachineDialog::~SelectMachineDialog() = default;

QString SelectMachineDialog::machine() const {
    const QModelIndex i = ui_->MachinesListView->currentIndex();
    return i.isValid() ? filter_->index(i.row(), 0).data(NameRole).toString() : QString();
}

int SelectMachineDialog::shownCount() const { return filter_->rowCount(); }

// ReselectItem: the machine, else the first one shown.
void SelectMachineDialog::selectMachine(const QString& name) {
    QTreeView* v = ui_->MachinesListView;
    QModelIndex sel;
    for (int r = 0; r < filter_->rowCount() && !sel.isValid(); ++r)
        if (filter_->index(r, 0).data(NameRole).toString() == name) sel = filter_->index(r, 0);
    if (!sel.isValid() && filter_->rowCount()) sel = filter_->index(0, 0);
    if (!sel.isValid()) return;
    v->setCurrentIndex(sel);
    v->scrollTo(sel, QAbstractItemView::PositionAtCenter);
    v->setFocus();
}

void SelectMachineDialog::applyFilters() {
    const QString keep = machine();
    filter_->availableOnly = ui_->ShowAvailableMachinesOnly->isChecked();
    filter_->hidePreliminary = ui_->HidePreliminaryMachines->isChecked();
    filter_->keep2 = keep;
    filter_->refresh();
    ui_->LabelTotalMachines->setText(tr("Total %1").arg(filter_->rowCount()));
    ui_->LabelTotalMachines->adjustSize();
    selectMachine(keep.isEmpty() ? in_.current : keep);
}

void SelectMachineDialog::done(int r) {
    WinIni ini;
    ini.load(in_.extrasIni);
    ini.writeInteger(kSection, "ShowAvailableMachinesOnly", ui_->ShowAvailableMachinesOnly->isChecked() ? 1 : 0);
    ini.writeInteger(kSection, "HidePreliminaryMachines", ui_->HidePreliminaryMachines->isChecked() ? 1 : 0);
    QDir().mkpath(QFileInfo(in_.extrasIni).absolutePath());
    ini.save(in_.extrasIni);
    QDialog::done(r);
}

} // namespace eld
