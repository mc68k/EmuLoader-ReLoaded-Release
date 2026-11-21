#include "CustomizeListDialog.h"
#include "FileProblem.h"

#include "ElWidgets.h"
#include "SoftwareLists.h"
#include "VclRuntime.h"
#include "ui_FormArcadeMAMEMachinesCustomize.h"
#include "ui_FormArcadeSoftwareListCustomize.h"

#include <QCheckBox>
#include <QDir>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTreeView>

namespace eld {

namespace {
enum Role { NameRole = Qt::UserRole + 1, ParentRole };
} // namespace

// Show Unchecked Only, Show Parent Sets Only
class CustomizeFilter : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    bool uncheckedOnly = false, parentsOnly = false;
    void refresh() { invalidateFilter(); }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        const QModelIndex i = sourceModel()->index(row, 0, parent);
        if (uncheckedOnly && i.data(Qt::CheckStateRole).toInt() == Qt::Checked) return false;
        if (parentsOnly && !i.data(ParentRole).toBool()) return false;
        return true;
    }
};

// The two forms' widgets, whichever is shown.
struct CustomizeListDialog::Ui {
    std::unique_ptr<::Ui::FormArcadeSoftwareListCustomize> soft;
    std::unique_ptr<::Ui::FormArcadeMAMEMachinesCustomize> machines;
    QLabel *total, *title, *version;
    ElImage *systemIcon, *emulatorIcon;
    QPushButton *yes, *no, *reset;
    QCheckBox *uncheckedOnly, *parentsOnly = nullptr, *checkAll;
    QTreeView* view;
    QWidget *topBar, *bottom;
};

CustomizeListDialog::CustomizeListDialog(const Input& in, QWidget* parent)
    : QDialog(parent), ui_(new Ui), in_(in) {
    Ui& u = *ui_;
    if (in_.kind == SoftwareLists) {
        u.soft.reset(new ::Ui::FormArcadeSoftwareListCustomize);
        auto& f = *u.soft;
        f.setupUi(this);
        u.total = f.LabelTotalSoftwareList; u.title = f.LabelSystemTitle; u.version = f.LabelEmulatorVersion;
        u.systemIcon = f.SystemIcon; u.emulatorIcon = f.EmulatorIcon;
        u.yes = f.ButtonYes; u.no = f.ButtonNo; u.reset = f.ButtonResetToCurrent;
        u.uncheckedOnly = f.FilterShowUncheckedOnly; u.checkAll = f.CheckAll;
        u.view = f.SoftwareLists; u.topBar = f.TopBar; u.bottom = f.PanelBottom;
    } else {
        u.machines.reset(new ::Ui::FormArcadeMAMEMachinesCustomize);
        auto& f = *u.machines;
        f.setupUi(this);
        u.total = f.LabelTotalMachinesList; u.title = f.LabelSystemTitle; u.version = f.LabelEmulatorVersion;
        u.systemIcon = f.SystemIcon; u.emulatorIcon = f.EmulatorIcon;
        u.yes = f.ButtonYes; u.no = f.ButtonNo; u.reset = f.ButtonResetToCurrent;
        u.uncheckedOnly = f.FilterShowUncheckedOnly; u.parentsOnly = f.FilterShowParentSetsOnly; u.checkAll = f.CheckAll;
        u.view = f.MachinesListEditor; u.topBar = f.TopBar; u.bottom = f.PanelBottom;
    }
    vcl::apply(this);
    const bool soft = in_.kind == SoftwareLists;
    setWindowTitle(soft ? tr("Customize MAME Software List") : tr("Customize MAME Machines List"));
    u.title->setWordWrap(true);
    u.title->setText(soft ? tr("Uncheck the software lists you want to leave out: their games aren't made or listed. "
                               "Create MAME Software Games List makes the lists you check again.")
                          : tr("Uncheck the machines you want to hide in the Machines List panel (Machines List Filter > "
                               "Show Enabled Only). Hiding a parent set hides its clones too. Right-click to check or "
                               "uncheck several selected machines."));
    u.version->setText(in_.emulator);
    u.systemIcon->setPixmap(in_.systemIcon.pixmap(68, 68));
    u.systemIcon->setAlignment(Qt::AlignCenter);
    u.emulatorIcon->setPixmap(in_.emulatorIcon.pixmap(24, 24));
    u.yes->setText(tr("OK"));
    u.yes->setToolTip(tr("Save the list in %1").arg(QDir::toNativeSeparators(in_.excludeFile)));
    u.no->setText(tr("Cancel"));
    u.reset->setText(tr("Reset"));
    u.reset->setToolTip(tr("Reload selections from last save"));
    u.uncheckedOnly->setText(tr("Show Unchecked Only"));
    u.uncheckedOnly->setToolTip(soft ? tr("Show only disabled software lists for easier viewing")
                                     : tr("Show only disabled machines for easier viewing"));
    if (u.parentsOnly) u.parentsOnly->setText(tr("Show Parent Sets Only"));

    // The rows: checked unless excluded
    const QStringList headers = soft ? QStringList{tr("Software List"), tr("Name"), tr("Media Type")}
                                     : QStringList{tr("Machine"), tr("Year"), tr("Manufacturer"), tr("Name"),
                                                   tr("Clone of"), tr("Driver"), tr("Save State")};
    model_ = new QStandardItemModel(0, int(headers.size()), this);
    model_->setHorizontalHeaderLabels(headers);
    for (const Row& r : in_.rows) {
        QList<QStandardItem*> items;
        for (int c = 0; c < headers.size(); ++c) items << new QStandardItem(r.cells.value(c));
        items[0]->setIcon(r.icon);
        items[0]->setCheckable(true);
        items[0]->setCheckState(in_.excluded.contains(r.name) ? Qt::Unchecked : Qt::Checked);
        items[0]->setData(r.name, NameRole);
        items[0]->setData(r.parent, ParentRole);
        if (r.gray)
            for (QStandardItem* it : items) it->setForeground(QColor(0x80, 0x80, 0x80));
        model_->appendRow(items);
    }
    filter_ = new CustomizeFilter(this);
    filter_->setSourceModel(model_);
    filter_->setSortCaseSensitivity(Qt::CaseInsensitive);
    QTreeView* v = u.view;
    v->setModel(filter_);
    v->setRootIsDecorated(false);
    v->setUniformRowHeights(true);
    v->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->setSelectionBehavior(QAbstractItemView::SelectRows);
    v->setSelectionMode(QAbstractItemView::ExtendedSelection);
    v->setIconSize(QSize(16, 16));
    v->setSortingEnabled(true);
    v->sortByColumn(0, Qt::AscendingOrder);
    if (soft) {
        v->setColumnWidth(0, 760);
        v->setColumnWidth(1, 170);
    } else {
        const int w[] = {380, 55, 160, 95, 95, 110}; // the rest for Save State, with no scroll bar
        for (int c = 0; c < 6; ++c) v->setColumnWidth(c, w[c]);
    }
    connect(model_, &QStandardItemModel::itemChanged, this, [this](QStandardItem* it) {
        if (it->column() == 0 && !updating_) updateCount();
    });
    // Check / Uncheck Multiple Selected (the machines' window; both here)
    v->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(v, &QWidget::customContextMenuRequested, this, [this, v](const QPoint& p) {
        QMenu m(this);
        QAction* check = m.addAction(tr("Check Multiple Selected"));
        m.addAction(tr("Uncheck Multiple Selected")); // (any other choice)
        QAction* a = m.exec(v->viewport()->mapToGlobal(p));
        if (!a) return;
        updating_ = true;
        for (const QModelIndex& i : v->selectionModel()->selectedRows())
            model_->itemFromIndex(filter_->mapToSource(i))->setCheckState(a == check ? Qt::Checked : Qt::Unchecked);
        updating_ = false;
        updateCount();
    });

    connect(u.checkAll, &QCheckBox::clicked, this, [this] { checkAll(ui_->checkAll->checkState() != Qt::Unchecked); });
    connect(u.uncheckedOnly, &QCheckBox::toggled, this, [this] { applyFilters(); });
    if (u.parentsOnly) connect(u.parentsOnly, &QCheckBox::toggled, this, [this] { applyFilters(); });
    connect(u.reset, &QPushButton::clicked, this, &CustomizeListDialog::reset);
    connect(u.yes, &QPushButton::clicked, this, &CustomizeListDialog::confirm);
    connect(u.no, &QPushButton::clicked, this, &QDialog::reject);
    for (QPushButton* b : findChildren<QPushButton*>()) b->setAutoDefault(false);
    u.yes->setDefault(true);
    u.reset->setEnabled(!in_.excluded.isEmpty());
    ui_->total->setAlignment(Qt::AlignRight | Qt::AlignVCenter); // (against the right edge)
    vcl::layoutForm(this); // every text whole in any language and size
    updateCount();
}

CustomizeListDialog::~CustomizeListDialog() = default;

QStringList CustomizeListDialog::excluded() const {
    QStringList out;
    for (int r = 0; r < model_->rowCount(); ++r)
        if (model_->item(r, 0)->checkState() != Qt::Checked) out << model_->item(r, 0)->data(NameRole).toString();
    out.sort();
    return out;
}

int CustomizeListDialog::checkedCount() const { return model_->rowCount() - int(excluded().size()); }
int CustomizeListDialog::shownCount() const { return filter_->rowCount(); }

void CustomizeListDialog::setChecked(const QString& name, bool on) {
    for (int r = 0; r < model_->rowCount(); ++r)
        if (model_->item(r, 0)->data(NameRole).toString() == name) model_->item(r, 0)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
}

// UpdateCheckedStateCheckBox: "Checked 12 of 40"; the check box checked, unchecked or partly.
void CustomizeListDialog::updateCount() {
    const int checked = checkedCount(), total = model_->rowCount();
    ui_->total->setText(tr("Checked %1 of %2").arg(checked).arg(total));
    ui_->checkAll->setTristate(checked > 0 && checked < total);
    ui_->checkAll->setCheckState(checked == 0 ? Qt::Unchecked : checked == total ? Qt::Checked : Qt::PartiallyChecked);
    ui_->checkAll->setText(checked == total ? tr("Uncheck All") : tr("Check All"));
    if (ui_->uncheckedOnly->isChecked()) filter_->refresh();
}

void CustomizeListDialog::checkAll(bool on) {
    // Uncheck All when they're all checked, else Check All
    const bool all = checkedCount() == model_->rowCount();
    Q_UNUSED(on);
    updating_ = true;
    for (int r = 0; r < model_->rowCount(); ++r) model_->item(r, 0)->setCheckState(all ? Qt::Unchecked : Qt::Checked);
    updating_ = false;
    updateCount();
}

void CustomizeListDialog::applyFilters() {
    filter_->uncheckedOnly = ui_->uncheckedOnly->isChecked();
    filter_->parentsOnly = ui_->parentsOnly && ui_->parentsOnly->isChecked();
    filter_->refresh();
}

// ButtonResetToCurrentClick: the checks as last saved
void CustomizeListDialog::reset() {
    updating_ = true;
    for (int r = 0; r < model_->rowCount(); ++r)
        model_->item(r, 0)->setCheckState(in_.excluded.contains(model_->item(r, 0)->data(NameRole).toString()) ? Qt::Unchecked
                                                                                                           : Qt::Checked);
    updating_ = false;
    updateCount();
}

// ButtonYesClick: the unchecked ones written to the excluded list (none: no file). At least one
// must stay checked, as the original asks.
void CustomizeListDialog::confirm() {
    if (checkedCount() == 0) {
        QMessageBox::information(this, windowTitle(), tr("Tick at least one: the list can't be left empty."));
        return;
    }
    if (!SoftwareLists::saveExcluded(in_.excludeFile, excluded())) {
        FileProblem::cantSave(this, windowTitle(), in_.excludeFile);
        return;
    }
    accept();
}

} // namespace eld
