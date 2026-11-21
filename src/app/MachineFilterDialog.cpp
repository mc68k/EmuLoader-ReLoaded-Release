#include "MachineFilterDialog.h"
#include "VclRuntime.h"
#include "WinIni.h"
#include "ui_FormSelectFilterSystemMega.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QMessageBox>
#include <QMouseEvent>
#include <QStandardItemModel>
#include <QUrl>

namespace eld {

namespace {
const char* kSection = "MachineType";
struct Row { const char* key; const char* icon; const char* title; const char* details; };
// AddMachinesFilters: the MAME family rows (console/computer/handheld *systems* are the
// separate EmuCon emulators, outside this port).
const Row kRows[] = {
    {"ArcadeSystems", "systemtype_arcade.ico", QT_TRANSLATE_NOOP("eld::MachineFilterDialog", "Arcade Systems"), QT_TRANSLATE_NOOP("eld::MachineFilterDialog", "Enable arcade games")},
    {"MAMEMachinesWithSoftwareList", "emu_mame.ico", QT_TRANSLATE_NOOP("eld::MachineFilterDialog", "MAME Machines With Software Lists"), QT_TRANSLATE_NOOP("eld::MachineFilterDialog", "Enable console/computer machines")},
    {"MAMEMachinesWithoutSoftwareList", "emu_mame.ico", QT_TRANSLATE_NOOP("eld::MachineFilterDialog", "MAME Machines Without Software Lists"), QT_TRANSLATE_NOOP("eld::MachineFilterDialog", "Enable console/computer machines")},
    {"MAMESoftwareListGames", "emu_mame.ico", QT_TRANSLATE_NOOP("eld::MachineFilterDialog", "MAME Software List Games"), QT_TRANSLATE_NOOP("eld::MachineFilterDialog", "Enable console/computer games")},
    {"EnableMAMEMachinesFilterPanel", "emu_mame.ico", QT_TRANSLATE_NOOP("eld::MachineFilterDialog", "Enable MAME Machines Filter Panel"), QT_TRANSLATE_NOOP("eld::MachineFilterDialog", "Filter games by machine")},
};
const int kRowCount = int(sizeof(kRows) / sizeof(kRows[0]));
bool* field(GamesFilter::MachineTypes& m, int row) {
    switch (row) {
    case 0: return &m.arcade;
    case 1: return &m.withSoftwareLists;
    case 2: return &m.withoutSoftwareLists;
    case 3: return &m.softwareGames;
    default: return &m.machinesPanel;
    }
}
} // namespace

GamesFilter::MachineTypes MachineFilterDialog::load(const QString& file) {
    GamesFilter::MachineTypes m;
    WinIni ini;
    if (!ini.load(file)) return m;
    for (int r = 0; r < kRowCount; ++r) *field(m, r) = ini.readBool(kSection, kRows[r].key, r != 4); // the panel is off by default
    return m;
}

bool MachineFilterDialog::save(const QString& file, const GamesFilter::MachineTypes& mt) {
    GamesFilter::MachineTypes m = mt;
    WinIni ini;
    ini.load(file); // keep the other machine types and the systems filters
    for (int r = 0; r < kRowCount; ++r) ini.writeInteger(kSection, kRows[r].key, *field(m, r) ? 1 : 0);
    QDir().mkpath(QFileInfo(file).absolutePath());
    return ini.save(file);
}

MachineFilterDialog::MachineFilterDialog(const GamesFilter::MachineTypes& current, const QString& iconFolder, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormSelectFilterSystemMega), initial_(current) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("Machine Type Filters"));
    ui_->LabelSelectMode->setText(tr("Choose what machine types you want to see in the games list."));

    model_ = new QStandardItemModel(this);
    model_->setHorizontalHeaderLabels({tr("Machine Type"), tr("Details")});
    QTreeView* v = ui_->MachinesTypeList;
    v->setModel(model_);
    v->setRootIsDecorated(false);
    v->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->setIconSize(QSize(24, 24));
    v->header()->hide();
    for (const Row& r : kRows) {
        auto* t = new QStandardItem(QIcon(QDir(iconFolder).filePath(r.icon)), tr(r.title));
        QFont f = t->font();
        f.setBold(true);
        t->setFont(f);
        t->setCheckable(true);
        model_->appendRow({t, new QStandardItem(r.details)});
    }
    v->header()->resizeSection(0, 360);
    fill(current);

    // "click here to download version.ini pack": mess.ini makes "Without Software Lists" work.
    ui_->LabelCategoryIniForMESS->setCursor(Qt::PointingHandCursor);
    ui_->LabelCategoryIniForMESS->installEventFilter(this);
    ui_->LabelCategoryIniForMESS->setToolTip("http://www.progettosnaps.net/renameset/");

    connect(ui_->ButtonOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui_->ButtonCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ui_->ButtonReset, &QPushButton::clicked, this, [this] { fill(initial_); });
    connect(ui_->ButtonHelp, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, tr("Machine Type Filters"),
            tr("Select the machine types you want to show or hide in the games list.<br><br>"
            "The <b>Arcade Systems</b> machine type includes the MAME games.<br>"
            "<b>MAME Machines With Software Lists</b> are the consoles and computers that run software lists.<br>"
            "The <b>MAME Machines Without Software Lists</b> option requires AntoPISA's <b>mess.ini</b> file "
            "(in the <b>arcade\\filters</b> folder) to work.<br>"
            "<b>MAME Software List Games</b> are the games of those lists (Menu Options > MAME/Arcade > "
            "Create MAME Software Games List).<br>"
            "<b>Enable MAME Machines Filter Panel</b> shows the MAME machines with software lists beside the games list: "
            "double click one (or press Enter) to list the games it runs.<br><br>"
            "Filter settings are saved in <b>ini_files\\games_filters.ini</b>."));
    });
    vcl::layoutForm(this); // every text whole in any language and size
}

MachineFilterDialog::~MachineFilterDialog() = default;

bool MachineFilterDialog::eventFilter(QObject* o, QEvent* e) {
    if (o == ui_->LabelCategoryIniForMESS && e->type() == QEvent::MouseButtonRelease) {
        QDesktopServices::openUrl(QUrl(ui_->LabelCategoryIniForMESS->toolTip()));
        return true;
    }
    return QDialog::eventFilter(o, e);
}

void MachineFilterDialog::fill(const GamesFilter::MachineTypes& mt) {
    GamesFilter::MachineTypes m = mt;
    for (int r = 0; r < kRowCount; ++r) model_->item(r, 0)->setCheckState(*field(m, r) ? Qt::Checked : Qt::Unchecked);
}

GamesFilter::MachineTypes MachineFilterDialog::machineTypes() const {
    GamesFilter::MachineTypes m;
    for (int r = 0; r < kRowCount; ++r) *field(m, r) = model_->item(r, 0)->checkState() == Qt::Checked;
    return m;
}

} // namespace eld
